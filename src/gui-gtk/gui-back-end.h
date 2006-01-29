/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *\
 * This file is part of Quarry.                                    *
 *                                                                 *
 * Copyright (C) 2003, 2004, 2005, 2006 Paul Pogonyshev.           *
 *                                                                 *
 * This program is free software; you can redistribute it and/or   *
 * modify it under the terms of the GNU General Public License as  *
 * published by the Free Software Foundation; either version 2 of  *
 * the License, or (at your option) any later version.             *
 *                                                                 *
 * This program is distributed in the hope that it will be useful, *
 * but WITHOUT ANY WARRANTY; without even the implied warranty of  *
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the   *
 * GNU General Public License for more details.                    *
 *                                                                 *
 * You should have received a copy of the GNU General Public       *
 * License along with this program; if not, write to the Free      *
 * Software Foundation, Inc., 51 Franklin Street, Fifth Floor,     *
 * Boston, MA 02110-1301, USA.                                     *
\* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */


#ifndef QUARRY_GTK_GUI_BACK_END_H
#define QUARRY_GTK_GUI_BACK_END_H


#include "quarry.h"


int		gui_back_end_init (int *argc, char **argv[]);
int		gui_back_end_main_default (void);
int		gui_back_end_main_open_files (int num_files, char **filenames);

/* We can't use `gpointer' here, because GTK+ headers are not visible
 * from `src/' directory.
 */
void		gui_back_end_register_object_to_finalize (void *object);
void		gui_back_end_register_pointer_to_free (void *pointer);


extern const char  *user_real_name;


#endif /* QUARRY_GTK_GUI_BACK_END_H */


/*
 * Local Variables:
 * tab-width: 8
 * c-basic-offset: 2
 * End:
 */
