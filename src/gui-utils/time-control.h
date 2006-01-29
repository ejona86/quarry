/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *\
 * This file is part of Quarry.                                    *
 *                                                                 *
 * Copyright (C) 2004, 2005, 2006 Paul Pogonyshev.                 *
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


#ifndef QUARRY_TIME_CONTROL_H
#define QUARRY_TIME_CONTROL_H


#include "sgf.h"
#include "quarry.h"


#define OUT_OF_TIME		(-1.0)
#define NO_TIME_LIMITS		  0.0


#define TIME_CONTROL_CLOCK_RUNS_DOWN(time_control)	\
  ((time_control)->main_time > 0			\
   || (time_control)->overtime_length > 0)


typedef struct _TimeControl	TimeControl;

struct _TimeControl {
  int		main_time;
  int		overtime_length;
  int		moves_per_overtime;

  double	seconds_elapsed;
  int		moves_to_play;

  void	       *timer_object;
  int		is_active;
};


TimeControl *	time_control_new (int main_time,
				  int overtime_length, int moves_per_overtime);
TimeControl *	time_control_new_from_sgf_node (const SgfNode *sgf_node);
inline TimeControl *
		time_control_duplicate (const TimeControl *time_control);
void		time_control_init (TimeControl *time_control,
				   int main_time,
				   int overtime_length,
				   int moves_per_overtime);

void		time_control_delete (TimeControl *time_control);
void		time_control_dispose (TimeControl *time_control);

void		time_control_save_settings_in_sgf_node
		  (const TimeControl *time_control,
		   SgfNode *sgf_node, SgfGameTree *sgf_tree);
void		time_control_save_state_in_sgf_node
		  (const TimeControl *time_control,
		   SgfNode *sgf_node, SgfGameTree *sgf_tree, int color);

void		time_control_apply_defaults_if_needed
		  (const TimeControl *time_control,
		   double seconds_left, int moves_to_play,
		   double *new_seconds_left, int *new_moves_to_play);

void		time_control_set_state (TimeControl *time_control,
					double seconds_left,
					int moves_to_play);

double		time_control_start (TimeControl *time_control);
double		time_control_get_clock_seconds
		  (const TimeControl *time_control,
		   int *moves_to_play);
double		time_control_stop (TimeControl *time_control,
				   int *moves_to_play);

double		time_control_get_time_left (const TimeControl *time_control,
					    int *moves_to_play);

double		time_control_get_time_till_seconds_update
		  (const TimeControl *time_control);

int		time_control_is_short_on_time
		  (const TimeControl *time_control);


/* GUI back-end specific functions (don't belong to GUI utils.) */
void *		gui_back_end_timer_restart (void *timer_object);
void		gui_back_end_timer_delete (void *timer_object);

double		gui_back_end_timer_get_seconds_elapsed (void *timer_object);


#endif /* QUARRY_TIME_CONTROL_H */


/*
 * Local Variables:
 * tab-width: 8
 * c-basic-offset: 2
 * End:
 */
