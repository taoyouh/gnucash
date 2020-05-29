/*
 * gnucash.cpp -- The program entry point for GnuCash
 *
 * Copyright (C) 2006 Chris Shoemaker <c.shoemaker@cox.net>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, contact:
 *
 * Free Software Foundation           Voice:  +1-617-542-5942
 * 51 Franklin Street, Fifth Floor    Fax:    +1-617-542-2652
 * Boston, MA  02110-1301,  USA       gnu@gnu.org
 */
#include <config.h>

#include <libguile.h>
#include <guile-mappings.h>
#ifdef __MINGW32__
#include <Windows.h>
#include <fcntl.h>
#endif

#include "gnucash-core-app.hpp"

extern "C" {
#include <dialog-new-user.h>
#include <gfec.h>
#include <gnc-file.h>
#include <gnc-filepath-utils.h>
#include <gnc-gnome-utils.h>
#include <gnc-gsettings.h>
#include <gnc-module.h>
#include <gnc-path.h>
#include <gnc-plugin-bi-import.h>
#include <gnc-plugin-csv-export.h>
#include <gnc-plugin-csv-import.h>
#include <gnc-plugin-customer-import.h>
#include <gnc-plugin-file-history.h>
#include <gnc-plugin-log-replay.h>
#include <gnc-plugin-qif-import.h>
#include <gnc-plugin-report-system.h>
#include <gnc-prefs.h>
#include <gnc-prefs-utils.h>
#include <gnc-report.h>
#include <gnc-session.h>
#include <gnc-splash.h>
#include <gnucash-register.h>
#include <search-core-type.h>
#include <top-level.h>
}

#include <boost/locale.hpp>
#include <iostream>
#include <gnc-locale-utils.hpp>

namespace bl = boost::locale;

/* This static indicates the debugging module that this .o belongs to.  */
static QofLogModule log_module = GNC_MOD_GUI;
static gchar *userdata_migration_msg = NULL;

static gboolean
try_load_config_array(const gchar *fns[])
{
    gchar *filename;
    int i;

    for (i = 0; fns[i]; i++)
    {
        filename = gnc_build_userdata_path(fns[i]);
        if (gfec_try_load(filename))
        {
            g_free(filename);
            return TRUE;
        }
        g_free(filename);
    }
    return FALSE;
}

static void
update_message(const gchar *msg)
{
    gnc_update_splash_screen(msg, GNC_SPLASH_PERCENTAGE_UNKNOWN);
    g_message("%s", msg);
}

static void
load_system_config(void)
{
    static int is_system_config_loaded = FALSE;
    gchar *system_config_dir;
    gchar *system_config;

    if (is_system_config_loaded) return;

    update_message("loading system configuration");
    system_config_dir = gnc_path_get_pkgsysconfdir();
    system_config = g_build_filename(system_config_dir, "config", NULL);
    is_system_config_loaded = gfec_try_load(system_config);
    g_free(system_config_dir);
    g_free(system_config);
}

static void
load_user_config(void)
{
    /* Don't continue adding to this list. When 3.0 rolls around bump
       the 2.4 files off the list. */
    static const gchar *saved_report_files[] =
    {
        SAVED_REPORTS_FILE, SAVED_REPORTS_FILE_OLD_REV, NULL
    };
    static const gchar *stylesheet_files[] = { "stylesheets-2.0", NULL};
    static int is_user_config_loaded = FALSE;

    if (is_user_config_loaded)
        return;
    else is_user_config_loaded = TRUE;

    update_message("loading user configuration");
    {
        gchar *config_filename;
        config_filename = g_build_filename (gnc_userconfig_dir (),
                                                "config-user.scm", (char *)NULL);
        gfec_try_load(config_filename);
        g_free(config_filename);
    }

    update_message("loading saved reports");
    try_load_config_array(saved_report_files);
    update_message("loading stylesheets");
    try_load_config_array(stylesheet_files);
}

static void
load_gnucash_plugins()
{
    gnc_plugin_bi_import_create_plugin ();
    gnc_plugin_csv_export_create_plugin ();
    gnc_plugin_csv_import_create_plugin();
    gnc_plugin_customer_import_create_plugin ();
    gnc_plugin_qif_import_create_plugin ();
    gnc_plugin_log_replay_create_plugin ();
}

static void
load_gnucash_modules()
{
    int i, len;
    struct
    {
        const gchar * name;
        int version;
        gboolean optional;
    } modules[] =
    {
        { "gnucash/import-export/ofx", 0, TRUE },
        { "gnucash/import-export/aqbanking", 0, TRUE },
        { "gnucash/python", 0, TRUE },
    };

    /* module initializations go here */
    len = sizeof(modules) / sizeof(*modules);
    for (i = 0; i < len; i++)
    {
        DEBUG("Loading module %s started", modules[i].name);
        gnc_update_splash_screen(modules[i].name, GNC_SPLASH_PERCENTAGE_UNKNOWN);
        if (modules[i].optional)
            gnc_module_load_optional(modules[i].name, modules[i].version);
        else
            gnc_module_load(modules[i].name, modules[i].version);
        DEBUG("Loading module %s finished", modules[i].name);
    }
}

static void
inner_main_add_price_quotes(void *data, [[maybe_unused]] int argc, [[maybe_unused]] char **argv)
{
    const char* add_quotes_file = static_cast<const char*>(data);
    SCM mod, add_quotes, scm_book, scm_result = SCM_BOOL_F;
    QofSession *session = NULL;

    scm_c_eval_string("(debug-set! stack 200000)");

    mod = scm_c_resolve_module("gnucash price-quotes");
    scm_set_current_module(mod);

    gnc_prefs_init ();
    qof_event_suspend();
    scm_c_eval_string("(gnc:price-quotes-install-sources)");

    if (!gnc_quote_source_fq_installed())
    {
        std::cerr << bl::translate ("No quotes retrieved. Finance::Quote isn't "
                                    "installed properly.") << "\n";
        goto fail;
    }

    add_quotes = scm_c_eval_string("gnc:book-add-quotes");
    session = gnc_get_current_session();
    if (!session) goto fail;

    qof_session_begin(session, add_quotes_file, FALSE, FALSE, FALSE);
    if (qof_session_get_error(session) != ERR_BACKEND_NO_ERR) goto fail;

    qof_session_load(session, NULL);
    if (qof_session_get_error(session) != ERR_BACKEND_NO_ERR) goto fail;

    scm_book = gnc_book_to_scm(qof_session_get_book(session));
    scm_result = scm_call_2(add_quotes, SCM_BOOL_F, scm_book);

    qof_session_save(session, NULL);
    if (qof_session_get_error(session) != ERR_BACKEND_NO_ERR) goto fail;

    qof_session_destroy(session);
    if (!scm_is_true(scm_result))
    {
        g_warning("Failed to add quotes to %s.", add_quotes_file);
        goto fail;
    }

    qof_event_resume();
    gnc_shutdown(0);
    return;
fail:
    if (session)
    {
        if (qof_session_get_error(session) != ERR_BACKEND_NO_ERR)
            g_warning("Session Error: %s",
                      qof_session_get_error_message(session));
        qof_session_destroy(session);
    }
    qof_event_resume();
    gnc_shutdown(1);
}

static char *
get_file_to_load (const char* file_to_load)
{
    if (file_to_load)
        return g_strdup(file_to_load);
    else
        /* Note history will always return a valid (possibly empty) string */
        return gnc_history_get_last();
}

extern SCM scm_init_sw_gnome_module(void);

struct t_file_spec {
    int nofile;
    const char *file_to_load;
};

static void
inner_main (void *data, [[maybe_unused]] int argc, [[maybe_unused]] char **argv)
{
    auto user_file_spec = static_cast<t_file_spec*>(data);
    SCM main_mod;
    char* fn = NULL;


    scm_c_eval_string("(debug-set! stack 200000)");

    main_mod = scm_c_resolve_module("gnucash utilities");
    scm_set_current_module(main_mod);
    scm_c_use_module("gnucash app-utils");

    /* Check whether the settings need a version update */
    gnc_gsettings_version_upgrade ();

    gnc_gnome_utils_init();
    gnc_search_core_initialize ();
    gnc_hook_add_dangler(HOOK_UI_SHUTDOWN, (GFunc)gnc_search_core_finalize, NULL, NULL);
    gnucash_register_add_cell_types ();
    gnc_report_init ();

    load_gnucash_plugins();
    load_gnucash_modules();

    /* Load the config before starting up the gui. This insures that
     * custom reports have been read into memory before the Reports
     * menu is created. */
    load_system_config();
    load_user_config();

    /* Setting-up the report menu must come after the module
     loading but before the gui initializat*ion. */
    gnc_plugin_report_system_new();

    /* TODO: After some more guile-extraction, this should happen even
       before booting guile.  */
    gnc_main_gui_init();

    gnc_hook_add_dangler(HOOK_UI_SHUTDOWN, (GFunc)gnc_file_quit, NULL, NULL);

    /* Install Price Quote Sources */
    auto msg = bl::translate ("Checking Finance::Quote...").str(gnc_get_locale());
    gnc_update_splash_screen (msg.c_str(), GNC_SPLASH_PERCENTAGE_UNKNOWN);
    scm_c_use_module("gnucash price-quotes");
    scm_c_eval_string("(gnc:price-quotes-install-sources)");

    gnc_hook_run(HOOK_STARTUP, NULL);

    if (!user_file_spec->nofile && (fn = get_file_to_load (user_file_spec->file_to_load)) && *fn )
    {
        auto msg = bl::translate ("Loading data...").str(gnc_get_locale());
        gnc_update_splash_screen (msg.c_str(), GNC_SPLASH_PERCENTAGE_UNKNOWN);
        gnc_file_open_file(gnc_get_splash_screen(), fn, /*open_readonly*/ FALSE);
        g_free(fn);
    }
    else if (gnc_prefs_get_bool(GNC_PREFS_GROUP_NEW_USER, GNC_PREF_FIRST_STARTUP))
    {
        g_free(fn); /* fn could be an empty string ("") */
        gnc_destroy_splash_screen();
        gnc_ui_new_user_dialog();
    }

    if (userdata_migration_msg)
    {
        GtkWidget *dialog = gtk_message_dialog_new(NULL, GTK_DIALOG_MODAL,
                                                   GTK_MESSAGE_INFO,
                                                   GTK_BUTTONS_OK,
                                                   "%s",
                                                   userdata_migration_msg);
        gnc_destroy_splash_screen();
        gtk_dialog_run(GTK_DIALOG(dialog));
        gtk_widget_destroy (dialog);
        g_free (userdata_migration_msg);
    }
    /* Ensure temporary preferences are temporary */
    gnc_prefs_reset_group (GNC_PREFS_GROUP_WARNINGS_TEMP);

    gnc_destroy_splash_screen();
    gnc_main_window_show_all_windows();

    gnc_hook_run(HOOK_UI_POST_STARTUP, NULL);
    gnc_ui_start_event_loop();
    gnc_hook_remove_dangler(HOOK_UI_SHUTDOWN, (GFunc)gnc_file_quit);

    gnc_shutdown(0);
    return;
}

namespace Gnucash {

    class Gnucash : public CoreApp
    {
    public:
        Gnucash (const char* app_name);
        void parse_command_line (int argc, char **argv);

        std::string get_quotes_file (void)
            { return m_quotes_file; }
    private:
        void configure_program_options (void);

        std::string m_gtk_help_msg;
        std::string m_quotes_file; // Deprecated will be removed in gnucash 5.0
    };

}

Gnucash::Gnucash::Gnucash (const char *app_name) : Gnucash::CoreApp (app_name)
{
    configure_program_options();
}


void
Gnucash::Gnucash::parse_command_line (int argc, char **argv)
{
    Gnucash::CoreApp::parse_command_line (argc, argv);

    if (m_opt_map["help-gtk"].as<bool>())
    {
        std::cout << m_gtk_help_msg;
        exit(0);
    }

    if (m_opt_map.count ("namespace"))
        gnc_prefs_set_namespace_regexp (m_opt_map["namespace"].
            as<std::string>().c_str());

    if (m_opt_map.count ("add-price-quotes"))
        m_quotes_file = m_opt_map["add-price-quotes"].as<std::string>();
}

// Define command line options specific to gnucash.
void
Gnucash::Gnucash::configure_program_options (void)
{
    // The g_option context dance below is done to be able to show a help message
    // for gtk's options. The options themselves are already parsed out by
    // gtk_init_check by the time this function is called though. So it really only
    // serves to be able to display a help message.
    auto context = g_option_context_new (tagline.c_str());
    auto gtk_options = gtk_get_option_group(FALSE);
    g_option_context_add_group (context, gtk_options);
    m_gtk_help_msg = g_option_context_get_help (context, FALSE, gtk_options);
    g_option_context_free (context);

    bpo::options_description app_options(_("Application Options"));
    app_options.add_options()
    ("nofile", bpo::bool_switch(),
     N_("Do not load the last file opened"))
    ("help-gtk",  bpo::bool_switch(),
     _("Show help for gtk options"))
    ("add-price-quotes", bpo::value<std::string>(),
     N_("Add price quotes to given GnuCash datafile.\n"
        "Note this option has been deprecated and will be removed in GnuCash 5.0.\n"
        "Please use \"gnucash-cli --add-price-quotes\" instead."))
    ("namespace", bpo::value<std::string>(),
     N_("Regular expression determining which namespace commodities will be retrieved"
        "Note this option has been deprecated and will be removed in GnuCash 5.0.\n"
        "Please use \"gnucash-cli --add-price-quotes\" instead."))
    ("input-file", bpo::value<std::string>(),
     N_("[datafile]"));

    m_pos_opt_desc.add("input-file", -1);

    m_opt_desc->add (app_options);
}

int
main(int argc, char ** argv)
{
    Gnucash::Gnucash application (argv[0]);

    /* We need to initialize gtk before looking up all modules */
    if(!gtk_init_check (&argc, &argv))
    {
        std::cerr << bl::format (bl::translate ("Run '{1} --help' to see a full list of available command line options.")) % *argv[0]
        << "\n"
        << bl::translate ("Error: could not initialize graphical user interface and option add-price-quotes was not set.\n"
        "       Perhaps you need to set the $DISPLAY environment variable ?");
        return 1;
    }

    application.parse_command_line (argc, argv);
    application.start();

    /* If asked via a command line parameter, fetch quotes only */
    auto quotes_file = application.get_quotes_file ();
    if (!quotes_file.empty())
    {
        scm_boot_guile (argc, argv, inner_main_add_price_quotes, (void*) quotes_file.c_str());
        exit (0);  /* never reached */
    }

    /* Now the module files are looked up, which might cause some library
    initialization to be run, hence gtk must be initialized beforehand. */
    gnc_module_system_init();

    gnc_gui_init();

    auto user_file_spec = t_file_spec {application.get_no_file (), application.get_file_to_load ()};
    scm_boot_guile (argc, argv, inner_main, &user_file_spec);
    exit(0); /* never reached */
}
