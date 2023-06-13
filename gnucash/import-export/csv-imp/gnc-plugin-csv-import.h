/*
 * gnc-plugin-csv-import.h -- csv import plugin
 * Copyright (C) 2011 Robert Fewell
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

#ifndef __GNC_PLUGIN_CSV_IMPORT_H
#define __GNC_PLUGIN_CSV_IMPORT_H

#include <gtk/gtk.h>

#include "gnc-plugin.h"

G_BEGIN_DECLS

/* type macros */
#define GNC_TYPE_PLUGIN_CSV_IMPORT            (gnc_plugin_csv_import_get_type ())
G_DECLARE_FINAL_TYPE (GncPluginCsvImport, gnc_plugin_csv_import, GNC, PLUGIN_CSV_IMPORT, GncPlugin)

#define GNC_PLUGIN_CSV_IMPORT_NAME "gnc-plugin-csv-import"

/* function prototypes */
GncPlugin *gnc_plugin_csv_import_new      (void);

void       gnc_plugin_csv_import_create_plugin (void);

G_END_DECLS

#endif /* __GNC_PLUGIN_CSV_IMPORT_H */
