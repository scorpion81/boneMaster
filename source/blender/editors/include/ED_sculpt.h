/*
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 *
 * The Original Code is Copyright (C) 2008 Blender Foundation.
 * All rights reserved.
 */

/** \file
 * \ingroup editors
 */

#ifndef __ED_SCULPT_H__
#define __ED_SCULPT_H__

struct ARegion;
struct ListBase;
struct Object;
struct RegionView3D;
struct UndoStep;
struct UndoType;
struct ViewContext;
struct bContext;
struct rcti;

/* sculpt.c */
void ED_operatortypes_sculpt(void);
void ED_sculpt_redraw_planes_get(float planes[4][4], struct ARegion *ar, struct Object *ob);
bool ED_sculpt_mask_box_select(struct bContext *C,
                               struct ViewContext *vc,
                               const struct rcti *rect,
                               bool select);

/* sculpt_undo.c */
void ED_sculpt_undosys_type(struct UndoType *ut);

void ED_sculpt_undo_geometry_begin(struct Object *ob);
void ED_sculpt_undo_geometry_end(struct Object *ob);

#endif /* __ED_SCULPT_H__ */
