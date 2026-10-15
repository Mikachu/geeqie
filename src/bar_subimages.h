/*
 * Copyright (C) 2004 John Ellis
 * Copyright (C) 2008 - 2016 The Geeqie Team
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef BAR_SUBIMAGES_H
#define BAR_SUBIMAGES_H

GtkWidget *bar_pane_subimages_new_from_config(const gchar **attribute_names,
                                              const gchar **attribute_values);
void bar_pane_subimages_update_from_config(GtkWidget *pane,
                                           const gchar **attribute_names,
                                           const gchar **attribute_values);

#endif
