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
 * The Original Code is Copyright (C) 2001-2002 by NaN Holding BV.
 * All rights reserved.
 */

/** \file
 * \ingroup edtransform
 */

#include <stdlib.h>
#include <math.h>
#include <float.h>
#include <stdio.h>

#include "PIL_time.h"

#include "DNA_scene_types.h"
#include "DNA_object_types.h"
#include "DNA_meshdata_types.h" /* Temporary, for snapping to other unselected meshes */
#include "DNA_node_types.h"
#include "DNA_space_types.h"
#include "DNA_screen_types.h"
#include "DNA_view3d_types.h"
#include "DNA_windowmanager_types.h"

#include "BLI_math.h"
#include "BLI_blenlib.h"
#include "BLI_utildefines.h"

#include "GPU_immediate.h"
#include "GPU_state.h"

#include "BKE_layer.h"
#include "BKE_object.h"
#include "BKE_anim.h" /* for duplis */
#include "BKE_context.h"
#include "BKE_editmesh.h"
#include "BKE_sequencer.h"
#include "BKE_main.h"

#include "RNA_access.h"

#include "WM_types.h"

#include "ED_image.h"
#include "ED_node.h"
#include "ED_uvedit.h"
#include "ED_view3d.h"
#include "ED_transform_snap_object_context.h"

#include "DEG_depsgraph.h"

#include "UI_resources.h"
#include "UI_view2d.h"

#include "MEM_guardedalloc.h"

#include "transform.h"

/* this should be passed as an arg for use in snap functions */
#undef BASACT

/* use half of flt-max so we can scale up without an exception */

/********************* PROTOTYPES ***********************/

static void setSnappingCallback(TransInfo *t);

static void ApplySnapTranslation(TransInfo *t, float vec[3]);
static void ApplySnapRotation(TransInfo *t, float *vec);
static void ApplySnapResize(TransInfo *t, float vec[2]);

/* static void CalcSnapGrid(TransInfo *t, float *vec); */
static void CalcSnapGeometry(TransInfo *t, float *vec);

static void TargetSnapMedian(TransInfo *t);
static void TargetSnapCenter(TransInfo *t);
static void TargetSnapClosest(TransInfo *t);
static void TargetSnapActive(TransInfo *t);

static float RotationBetween(TransInfo *t, const float p1[3], const float p2[3]);
static float TranslationBetween(TransInfo *t, const float p1[3], const float p2[3]);
static float ResizeBetween(TransInfo *t, const float p1[3], const float p2[3]);

/****************** IMPLEMENTATIONS *********************/

static bool snapNodeTest(View2D *v2d, bNode *node, eSnapSelect snap_select);
static NodeBorder snapNodeBorder(int snap_node_mode);

#if 0
int BIF_snappingSupported(Object *obedit)
{
  int status = 0;

  /* only support object mesh, armature, curves */
  if (obedit == NULL || ELEM(obedit->type, OB_MESH, OB_ARMATURE, OB_CURVE, OB_LATTICE, OB_MBALL)) {
    status = 1;
  }

  return status;
}
#endif

bool validSnap(const TransInfo *t)
{
  return (t->tsnap.status & (POINT_INIT | TARGET_INIT)) == (POINT_INIT | TARGET_INIT) ||
         (t->tsnap.status & (MULTI_POINTS | TARGET_INIT)) == (MULTI_POINTS | TARGET_INIT);
}

bool activeSnap(const TransInfo *t)
{
  return ((t->modifiers & (MOD_SNAP | MOD_SNAP_INVERT)) == MOD_SNAP) ||
         ((t->modifiers & (MOD_SNAP | MOD_SNAP_INVERT)) == MOD_SNAP_INVERT);
}

bool transformModeUseSnap(const TransInfo *t)
{
  ToolSettings *ts = t->settings;
  if (t->mode == TFM_TRANSLATION) {
    return (ts->snap_transform_mode_flag & SCE_SNAP_TRANSFORM_MODE_TRANSLATE) != 0;
  }
  if (t->mode == TFM_ROTATION) {
    return (ts->snap_transform_mode_flag & SCE_SNAP_TRANSFORM_MODE_ROTATE) != 0;
  }
  if (t->mode == TFM_RESIZE) {
    return (ts->snap_transform_mode_flag & SCE_SNAP_TRANSFORM_MODE_SCALE) != 0;
  }

  return false;
}

static bool doForceIncrementSnap(const TransInfo *t)
{
  return !transformModeUseSnap(t);
}

void drawSnapping(const struct bContext *C, TransInfo *t)
{
  unsigned char col[4], selectedCol[4], activeCol[4];

  if (!activeSnap(t)) {
    return;
  }

  UI_GetThemeColor3ubv(TH_TRANSFORM, col);
  col[3] = 128;

  UI_GetThemeColor3ubv(TH_SELECT, selectedCol);
  selectedCol[3] = 128;

  UI_GetThemeColor3ubv(TH_ACTIVE, activeCol);
  activeCol[3] = 192;

  if (t->spacetype == SPACE_VIEW3D) {
    if (validSnap(t)) {
      TransSnapPoint *p;
      RegionView3D *rv3d = CTX_wm_region_view3d(C);
      float imat[4][4];
      float size;

      GPU_depth_test(false);

      size = 2.5f * UI_GetThemeValuef(TH_VERTEX_SIZE);

      invert_m4_m4(imat, rv3d->viewmat);

      uint pos = GPU_vertformat_attr_add(
          immVertexFormat(), "pos", GPU_COMP_F32, 3, GPU_FETCH_FLOAT);

      immBindBuiltinProgram(GPU_SHADER_3D_UNIFORM_COLOR);

      for (p = t->tsnap.points.first; p; p = p->next) {
        if (p == t->tsnap.selectedPoint) {
          immUniformColor4ubv(selectedCol);
        }
        else {
          immUniformColor4ubv(col);
        }

        imm_drawcircball(p->co, ED_view3d_pixel_size(rv3d, p->co) * size * 0.75f, imat, pos);
      }

      if (t->tsnap.status & POINT_INIT) {
        immUniformColor4ubv(activeCol);

        imm_drawcircball(
            t->tsnap.snapPoint, ED_view3d_pixel_size(rv3d, t->tsnap.snapPoint) * size, imat, pos);
      }

      /* draw normal if needed */
      if (usingSnappingNormal(t) && validSnappingNormal(t)) {
        immUniformColor4ubv(activeCol);

        immBegin(GPU_PRIM_LINES, 2);
        immVertex3f(pos, t->tsnap.snapPoint[0], t->tsnap.snapPoint[1], t->tsnap.snapPoint[2]);
        immVertex3f(pos,
                    t->tsnap.snapPoint[0] + t->tsnap.snapNormal[0],
                    t->tsnap.snapPoint[1] + t->tsnap.snapNormal[1],
                    t->tsnap.snapPoint[2] + t->tsnap.snapNormal[2]);
        immEnd();
      }

      immUnbindProgram();

      GPU_depth_test(true);
    }
  }
  else if (t->spacetype == SPACE_IMAGE) {
    if (validSnap(t)) {
      /* This will not draw, and Im nor sure why - campbell */
      /* TODO: see 2.7x for non-working code */
    }
  }
  else if (t->spacetype == SPACE_NODE) {
    if (validSnap(t)) {
      ARegion *ar = CTX_wm_region(C);
      TransSnapPoint *p;
      float size;

      size = 2.5f * UI_GetThemeValuef(TH_VERTEX_SIZE);

      GPU_blend(true);

      uint pos = GPU_vertformat_attr_add(
          immVertexFormat(), "pos", GPU_COMP_F32, 2, GPU_FETCH_FLOAT);

      immBindBuiltinProgram(GPU_SHADER_2D_UNIFORM_COLOR);

      for (p = t->tsnap.points.first; p; p = p->next) {
        if (p == t->tsnap.selectedPoint) {
          immUniformColor4ubv(selectedCol);
        }
        else {
          immUniformColor4ubv(col);
        }

        ED_node_draw_snap(&ar->v2d, p->co, size, 0, pos);
      }

      if (t->tsnap.status & POINT_INIT) {
        immUniformColor4ubv(activeCol);

        ED_node_draw_snap(&ar->v2d, t->tsnap.snapPoint, size, t->tsnap.snapNodeBorder, pos);
      }

      immUnbindProgram();

      GPU_blend(false);
    }
  }
}

eRedrawFlag handleSnapping(TransInfo *t, const wmEvent *event)
{
  eRedrawFlag status = TREDRAW_NOTHING;

#if 0  // XXX need a proper selector for all snap mode
  if (BIF_snappingSupported(t->obedit) && event->type == TABKEY && event->shift) {
    /* toggle snap and reinit */
    t->settings->snap_flag ^= SCE_SNAP;
    initSnapping(t, NULL);
    status = TREDRAW_HARD;
  }
#endif
  if (event->type == MOUSEMOVE) {
    status |= updateSelectedSnapPoint(t);
  }

  return status;
}

void applyProject(TransInfo *t)
{
  /* XXX FLICKER IN OBJECT MODE */
  if ((t->tsnap.project) && activeSnap(t) && (t->flag & T_NO_PROJECT) == 0) {
    float tvec[3];
    int i;

    FOREACH_TRANS_DATA_CONTAINER (t, tc) {
      TransData *td = tc->data;
      for (i = 0; i < tc->data_len; i++, td++) {
        float iloc[3], loc[3], no[3];
        float mval_fl[2];

        if (td->flag & TD_NOACTION) {
          break;
        }

        if (td->flag & TD_SKIP) {
          continue;
        }

        if ((t->flag & T_PROP_EDIT) && (td->factor == 0.0f)) {
          continue;
        }

        copy_v3_v3(iloc, td->loc);
        if (tc->use_local_mat) {
          mul_m4_v3(tc->mat, iloc);
        }
        else if (t->flag & T_OBJECT) {
          BKE_object_eval_transform_all(t->depsgraph, t->scene, td->ob);
          copy_v3_v3(iloc, td->ob->obmat[3]);
        }

        if (ED_view3d_project_float_global(t->ar, iloc, mval_fl, V3D_PROJ_TEST_NOP) ==
            V3D_PROJ_RET_OK) {
          if (ED_transform_snap_object_project_view3d(
                  t->tsnap.object_context,
                  SCE_SNAP_MODE_FACE,
                  &(const struct SnapObjectParams){
                      .snap_select = t->tsnap.modeSelect,
                      .use_object_edit_cage = (t->flag & T_EDIT) != 0,
                      .use_occlusion_test = false,
                  },
                  mval_fl,
                  NULL,
                  0,
                  loc,
                  no)) {
#if 0
            if (tc->use_local_mat) {
              mul_m4_v3(tc->imat, loc);
            }
#endif

            sub_v3_v3v3(tvec, loc, iloc);

            mul_m3_v3(td->smtx, tvec);

            add_v3_v3(td->loc, tvec);

            if (t->tsnap.align && (t->flag & T_OBJECT)) {
              /* handle alignment as well */
              const float *original_normal;
              float mat[3][3];

              /* In pose mode, we want to align normals with Y axis of bones... */
              original_normal = td->axismtx[2];

              rotation_between_vecs_to_mat3(mat, original_normal, no);

              transform_data_ext_rotate(td, mat, true);

              /* TODO support constraints for rotation too? see ElementRotation */
            }
          }
        }

        // XXX constraintTransLim(t, td);
      }
    }
  }
}

void applyGridAbsolute(TransInfo *t)
{
  float grid_size = 0.0f;
  GearsType grid_action;
  int i;

  if (!(activeSnap(t) && (t->tsnap.mode & (SCE_SNAP_MODE_INCREMENT | SCE_SNAP_MODE_GRID)))) {
    return;
  }

  grid_action = BIG_GEARS;
  if (t->modifiers & MOD_PRECISION) {
    grid_action = SMALL_GEARS;
  }

  switch (grid_action) {
    case NO_GEARS:
      grid_size = t->snap_spatial[0];
      break;
    case BIG_GEARS:
      grid_size = t->snap_spatial[1];
      break;
    case SMALL_GEARS:
      grid_size = t->snap_spatial[2];
      break;
  }
  /* early exit on unusable grid size */
  if (grid_size == 0.0f) {
    return;
  }

  FOREACH_TRANS_DATA_CONTAINER (t, tc) {
    TransData *td;

    for (i = 0, td = tc->data; i < tc->data_len; i++, td++) {
      float iloc[3], loc[3], tvec[3];

      if (td->flag & TD_NOACTION) {
        break;
      }

      if (td->flag & TD_SKIP) {
        continue;
      }

      if ((t->flag & T_PROP_EDIT) && (td->factor == 0.0f)) {
        continue;
      }

      copy_v3_v3(iloc, td->loc);
      if (tc->use_local_mat) {
        mul_m4_v3(tc->mat, iloc);
      }
      else if (t->flag & T_OBJECT) {
        BKE_object_eval_transform_all(t->depsgraph, t->scene, td->ob);
        copy_v3_v3(iloc, td->ob->obmat[3]);
      }

      mul_v3_v3fl(loc, iloc, 1.0f / grid_size);
      loc[0] = roundf(loc[0]);
      loc[1] = roundf(loc[1]);
      loc[2] = roundf(loc[2]);
      mul_v3_fl(loc, grid_size);

      sub_v3_v3v3(tvec, loc, iloc);
      mul_m3_v3(td->smtx, tvec);
      add_v3_v3(td->loc, tvec);
    }
  }
}

void applySnapping(TransInfo *t, float *vec)
{
  /* Each Trans Data already makes the snap to face */
  if (doForceIncrementSnap(t) || (t->tsnap.project && t->tsnap.mode == SCE_SNAP_MODE_FACE)) {
    return;
  }

  if (t->tsnap.status & SNAP_FORCED) {
    t->tsnap.targetSnap(t);

    t->tsnap.applySnap(t, vec);
  }
  else if (((t->tsnap.mode & ~(SCE_SNAP_MODE_INCREMENT | SCE_SNAP_MODE_GRID)) != 0) &&
           activeSnap(t)) {
    double current = PIL_check_seconds_timer();

    // Time base quirky code to go around findnearest slowness
    /* !TODO! add exception for object mode, no need to slow it down then */
    if (current - t->tsnap.last >= 0.01) {
      t->tsnap.calcSnap(t, vec);
      t->tsnap.targetSnap(t);

      t->tsnap.last = current;
    }
    if (validSnap(t)) {
      t->tsnap.applySnap(t, vec);
    }
  }
}

void resetSnapping(TransInfo *t)
{
  t->tsnap.status = 0;
  t->tsnap.align = false;
  t->tsnap.project = 0;
  t->tsnap.mode = 0;
  t->tsnap.modeSelect = 0;
  t->tsnap.target = 0;
  t->tsnap.last = 0;
  t->tsnap.applySnap = NULL;

  t->tsnap.snapNormal[0] = 0;
  t->tsnap.snapNormal[1] = 0;
  t->tsnap.snapNormal[2] = 0;

  t->tsnap.snapNodeBorder = 0;
}

bool usingSnappingNormal(const TransInfo *t)
{
  return t->tsnap.align;
}

bool validSnappingNormal(const TransInfo *t)
{
  if (validSnap(t)) {
    if (!is_zero_v3(t->tsnap.snapNormal)) {
      return true;
    }
  }

  return false;
}

static bool bm_edge_is_snap_target(BMEdge *e, void *UNUSED(user_data))
{
  if (BM_elem_flag_test(e, BM_ELEM_SELECT | BM_ELEM_HIDDEN) ||
      BM_elem_flag_test(e->v1, BM_ELEM_SELECT) || BM_elem_flag_test(e->v2, BM_ELEM_SELECT)) {
    return false;
  }

  return true;
}

static bool bm_face_is_snap_target(BMFace *f, void *UNUSED(user_data))
{
  if (BM_elem_flag_test(f, BM_ELEM_SELECT | BM_ELEM_HIDDEN)) {
    return false;
  }

  BMLoop *l_iter, *l_first;
  l_iter = l_first = BM_FACE_FIRST_LOOP(f);
  do {
    if (BM_elem_flag_test(l_iter->v, BM_ELEM_SELECT)) {
      return false;
    }
  } while ((l_iter = l_iter->next) != l_first);

  return true;
}

static void initSnappingMode(TransInfo *t)
{
  Main *bmain = CTX_data_main(t->context);
  ToolSettings *ts = t->settings;
  /* All obedit types will match. */
  const int obedit_type = t->data_container->obedit ? t->data_container->obedit->type : -1;
  ViewLayer *view_layer = t->view_layer;
  Base *base_act = view_layer->basact;

  if (t->spacetype == SPACE_NODE) {
    /* force project off when not supported */
    t->tsnap.project = 0;

    t->tsnap.mode = ts->snap_node_mode;
  }
  else if (t->spacetype == SPACE_IMAGE) {
    /* force project off when not supported */
    t->tsnap.project = 0;

    t->tsnap.mode = ts->snap_uv_mode;
  }
  else {
    /* force project off when not supported */
    if ((ts->snap_mode & SCE_SNAP_MODE_FACE) == 0) {
      t->tsnap.project = 0;
    }

    t->tsnap.mode = ts->snap_mode;
  }

  if ((t->spacetype == SPACE_VIEW3D || t->spacetype == SPACE_IMAGE) && /* Only 3D view or UV */
      (t->flag & T_CAMERA) == 0) /* Not with camera selected in camera view */
  {
    setSnappingCallback(t);

    /* Edit mode */
    if (t->tsnap.applySnap != NULL &&  // A snapping function actually exist
        ((obedit_type != -1) &&
         /* Temporary limited to edit mode meshes, armature, curves, metaballs. */
         ELEM(obedit_type, OB_MESH, OB_ARMATURE, OB_CURVE, OB_LATTICE, OB_MBALL))) {
      /* Exclude editmesh if using proportional edit */
      if ((obedit_type == OB_MESH) && (t->flag & T_PROP_EDIT)) {
        t->tsnap.modeSelect = SNAP_NOT_ACTIVE;
      }
      else {
        t->tsnap.modeSelect = t->tsnap.snap_self ? SNAP_ALL : SNAP_NOT_ACTIVE;
      }
    }
    /* Particles edit mode*/
    else if (t->tsnap.applySnap != NULL &&  // A snapping function actually exist
             ((obedit_type == -1) && base_act && base_act->object &&
              base_act->object->mode & OB_MODE_PARTICLE_EDIT)) {
      t->tsnap.modeSelect = SNAP_ALL;
    }
    /* Object mode */
    else if (t->tsnap.applySnap != NULL &&  // A snapping function actually exist
             (obedit_type == -1))           // Object Mode
    {
      /* In "Edit Strokes" mode,
       * snap tool can perform snap to selected or active objects (see T49632)
       * TODO: perform self snap in gpencil_strokes */
      t->tsnap.modeSelect = (((t->options & (CTX_GPENCIL_STROKES | CTX_CURSOR)) != 0) ?
                                 SNAP_ALL :
                                 SNAP_NOT_SELECTED);
    }
    else {
      /* Grid if snap is not possible */
      t->tsnap.mode = SCE_SNAP_MODE_INCREMENT;
    }
  }
  else if (t->spacetype == SPACE_NODE) {
    setSnappingCallback(t);

    if (t->tsnap.applySnap != NULL) {
      t->tsnap.modeSelect = SNAP_NOT_SELECTED;
    }
    else {
      /* Grid if snap is not possible */
      t->tsnap.mode = SCE_SNAP_MODE_INCREMENT;
    }
  }
  else if (t->spacetype == SPACE_SEQ) {
    /* We do our own snapping currently, so nothing here */
    t->tsnap.mode = SCE_SNAP_MODE_GRID; /* Dummy, should we rather add a NOP mode? */
  }
  else {
    /* Always grid outside of 3D view */
    t->tsnap.mode = SCE_SNAP_MODE_INCREMENT;
  }

  if (t->spacetype == SPACE_VIEW3D) {
    if (t->tsnap.object_context == NULL) {
      t->tsnap.object_context = ED_transform_snap_object_context_create_view3d(
          bmain, t->scene, t->depsgraph, 0, t->ar, t->view);

      ED_transform_snap_object_context_set_editmesh_callbacks(
          t->tsnap.object_context,
          (bool (*)(BMVert *, void *))BM_elem_cb_check_hflag_disabled,
          bm_edge_is_snap_target,
          bm_face_is_snap_target,
          POINTER_FROM_UINT((BM_ELEM_SELECT | BM_ELEM_HIDDEN)));
    }
  }
}

void initSnapping(TransInfo *t, wmOperator *op)
{
  ToolSettings *ts = t->settings;
  short snap_target = t->settings->snap_target;

  resetSnapping(t);

  /* if snap property exists */
  if (op && RNA_struct_find_property(op->ptr, "snap") &&
      RNA_struct_property_is_set(op->ptr, "snap")) {
    if (RNA_boolean_get(op->ptr, "snap")) {
      t->modifiers |= MOD_SNAP;

      if (RNA_struct_property_is_set(op->ptr, "snap_target")) {
        snap_target = RNA_enum_get(op->ptr, "snap_target");
      }

      if (RNA_struct_property_is_set(op->ptr, "snap_point")) {
        RNA_float_get_array(op->ptr, "snap_point", t->tsnap.snapPoint);
        t->tsnap.status |= SNAP_FORCED | POINT_INIT;
      }

      /* snap align only defined in specific cases */
      if (RNA_struct_find_property(op->ptr, "snap_align")) {
        t->tsnap.align = RNA_boolean_get(op->ptr, "snap_align");
        RNA_float_get_array(op->ptr, "snap_normal", t->tsnap.snapNormal);
        normalize_v3(t->tsnap.snapNormal);
      }

      if (RNA_struct_find_property(op->ptr, "use_snap_project")) {
        t->tsnap.project = RNA_boolean_get(op->ptr, "use_snap_project");
      }

      if (RNA_struct_find_property(op->ptr, "use_snap_self")) {
        t->tsnap.snap_self = RNA_boolean_get(op->ptr, "use_snap_self");
      }
    }
  }
  /* use scene defaults only when transform is modal */
  else if (t->flag & T_MODAL) {
    if (ELEM(t->spacetype, SPACE_VIEW3D, SPACE_IMAGE, SPACE_NODE)) {
      if (transformModeUseSnap(t) && (ts->snap_flag & SCE_SNAP)) {
        t->modifiers |= MOD_SNAP;
      }

      t->tsnap.align = ((t->settings->snap_flag & SCE_SNAP_ROTATE) != 0);
      t->tsnap.project = ((t->settings->snap_flag & SCE_SNAP_PROJECT) != 0);
      t->tsnap.snap_self = !((t->settings->snap_flag & SCE_SNAP_NO_SELF) != 0);
      t->tsnap.peel = ((t->settings->snap_flag & SCE_SNAP_PROJECT) != 0);
    }

    /* for now only 3d view (others can be added if we want) */
    if (t->spacetype == SPACE_VIEW3D) {
      t->tsnap.snap_spatial_grid = ((t->settings->snap_flag & SCE_SNAP_ABS_GRID) != 0);
    }
  }

  t->tsnap.target = snap_target;

  initSnappingMode(t);
}

void freeSnapping(TransInfo *t)
{
  if (t->tsnap.object_context) {
    ED_transform_snap_object_context_destroy(t->tsnap.object_context);
    t->tsnap.object_context = NULL;
  }
}

static void setSnappingCallback(TransInfo *t)
{
  t->tsnap.calcSnap = CalcSnapGeometry;

  switch (t->tsnap.target) {
    case SCE_SNAP_TARGET_CLOSEST:
      t->tsnap.targetSnap = TargetSnapClosest;
      break;
    case SCE_SNAP_TARGET_CENTER:
      t->tsnap.targetSnap = TargetSnapCenter;
      break;
    case SCE_SNAP_TARGET_MEDIAN:
      t->tsnap.targetSnap = TargetSnapMedian;
      break;
    case SCE_SNAP_TARGET_ACTIVE:
      t->tsnap.targetSnap = TargetSnapActive;
      break;
  }

  switch (t->mode) {
    case TFM_TRANSLATION:
      t->tsnap.applySnap = ApplySnapTranslation;
      t->tsnap.distance = TranslationBetween;
      break;
    case TFM_ROTATION:
      t->tsnap.applySnap = ApplySnapRotation;
      t->tsnap.distance = RotationBetween;

      // Can't do TARGET_CENTER with rotation, use TARGET_MEDIAN instead
      if (t->tsnap.target == SCE_SNAP_TARGET_CENTER) {
        t->tsnap.target = SCE_SNAP_TARGET_MEDIAN;
        t->tsnap.targetSnap = TargetSnapMedian;
      }
      break;
    case TFM_RESIZE:
      t->tsnap.applySnap = ApplySnapResize;
      t->tsnap.distance = ResizeBetween;

      // Can't do TARGET_CENTER with resize, use TARGET_MEDIAN instead
      if (t->tsnap.target == SCE_SNAP_TARGET_CENTER) {
        t->tsnap.target = SCE_SNAP_TARGET_MEDIAN;
        t->tsnap.targetSnap = TargetSnapMedian;
      }
      break;
    default:
      t->tsnap.applySnap = NULL;
      break;
  }
}

void addSnapPoint(TransInfo *t)
{
  /* Currently only 3D viewport works for snapping points. */
  if (t->tsnap.status & POINT_INIT && t->spacetype == SPACE_VIEW3D) {
    TransSnapPoint *p = MEM_callocN(sizeof(TransSnapPoint), "SnapPoint");

    t->tsnap.selectedPoint = p;

    copy_v3_v3(p->co, t->tsnap.snapPoint);

    BLI_addtail(&t->tsnap.points, p);

    t->tsnap.status |= MULTI_POINTS;
  }
}

eRedrawFlag updateSelectedSnapPoint(TransInfo *t)
{
  eRedrawFlag status = TREDRAW_NOTHING;

  if (t->tsnap.status & MULTI_POINTS) {
    TransSnapPoint *p, *closest_p = NULL;
    float dist_min_sq = TRANSFORM_SNAP_MAX_PX;
    const float mval_fl[2] = {t->mval[0], t->mval[1]};
    float screen_loc[2];

    for (p = t->tsnap.points.first; p; p = p->next) {
      float dist_sq;

      if (ED_view3d_project_float_global(t->ar, p->co, screen_loc, V3D_PROJ_TEST_NOP) !=
          V3D_PROJ_RET_OK) {
        continue;
      }

      dist_sq = len_squared_v2v2(mval_fl, screen_loc);

      if (dist_sq < dist_min_sq) {
        closest_p = p;
        dist_min_sq = dist_sq;
      }
    }

    if (closest_p) {
      if (t->tsnap.selectedPoint != closest_p) {
        status = TREDRAW_HARD;
      }

      t->tsnap.selectedPoint = closest_p;
    }
  }

  return status;
}

void removeSnapPoint(TransInfo *t)
{
  if (t->tsnap.status & MULTI_POINTS) {
    updateSelectedSnapPoint(t);

    if (t->tsnap.selectedPoint) {
      BLI_freelinkN(&t->tsnap.points, t->tsnap.selectedPoint);

      if (BLI_listbase_is_empty(&t->tsnap.points)) {
        t->tsnap.status &= ~MULTI_POINTS;
      }

      t->tsnap.selectedPoint = NULL;
    }
  }
}

void getSnapPoint(const TransInfo *t, float vec[3])
{
  if (t->tsnap.points.first) {
    TransSnapPoint *p;
    int total = 0;

    vec[0] = vec[1] = vec[2] = 0;

    for (p = t->tsnap.points.first; p; p = p->next, total++) {
      add_v3_v3(vec, p->co);
    }

    if (t->tsnap.status & POINT_INIT) {
      add_v3_v3(vec, t->tsnap.snapPoint);
      total++;
    }

    mul_v3_fl(vec, 1.0f / total);
  }
  else {
    copy_v3_v3(vec, t->tsnap.snapPoint);
  }
}

/********************** APPLY **************************/

static void ApplySnapTranslation(TransInfo *t, float vec[3])
{
  float point[3];
  getSnapPoint(t, point);

  if (t->spacetype == SPACE_NODE) {
    char border = t->tsnap.snapNodeBorder;
    if (border & (NODE_LEFT | NODE_RIGHT)) {
      vec[0] = point[0] - t->tsnap.snapTarget[0];
    }
    if (border & (NODE_BOTTOM | NODE_TOP)) {
      vec[1] = point[1] - t->tsnap.snapTarget[1];
    }
  }
  else {
    if (t->spacetype == SPACE_VIEW3D) {
      if (t->options & CTX_PAINT_CURVE) {
        if (ED_view3d_project_float_global(t->ar, point, point, V3D_PROJ_TEST_NOP) !=
            V3D_PROJ_RET_OK) {
          zero_v3(point); /* no good answer here... */
        }
      }
    }

    sub_v3_v3v3(vec, point, t->tsnap.snapTarget);
  }
}

static void ApplySnapRotation(TransInfo *t, float *value)
{
  float point[3];
  getSnapPoint(t, point);

  float dist = RotationBetween(t, t->tsnap.snapTarget, point);
  *value = dist;
}

static void ApplySnapResize(TransInfo *t, float vec[3])
{
  float point[3];
  getSnapPoint(t, point);

  float dist = ResizeBetween(t, t->tsnap.snapTarget, point);
  if (dist != TRANSFORM_DIST_INVALID) {
    copy_v3_fl(vec, dist);
  }
}

/********************** DISTANCE **************************/

static float TranslationBetween(TransInfo *UNUSED(t), const float p1[3], const float p2[3])
{
  return len_squared_v3v3(p1, p2);
}

static float RotationBetween(TransInfo *t, const float p1[3], const float p2[3])
{
  float angle, start[3], end[3];

  sub_v3_v3v3(start, p1, t->center_global);
  sub_v3_v3v3(end, p2, t->center_global);

  // Angle around a constraint axis (error prone, will need debug)
  if (t->con.applyRot != NULL && (t->con.mode & CON_APPLY)) {
    float axis[3], tmp[3];

    t->con.applyRot(t, NULL, NULL, axis, NULL);

    project_v3_v3v3(tmp, end, axis);
    sub_v3_v3v3(end, end, tmp);

    project_v3_v3v3(tmp, start, axis);
    sub_v3_v3v3(start, start, tmp);

    normalize_v3(end);
    normalize_v3(start);

    cross_v3_v3v3(tmp, start, end);

    if (dot_v3v3(tmp, axis) < 0.0f) {
      angle = -acosf(dot_v3v3(start, end));
    }
    else {
      angle = acosf(dot_v3v3(start, end));
    }
  }
  else {
    float mtx[3][3];

    copy_m3_m4(mtx, t->viewmat);

    mul_m3_v3(mtx, end);
    mul_m3_v3(mtx, start);

    angle = atan2f(start[1], start[0]) - atan2f(end[1], end[0]);
  }

  if (angle > (float)M_PI) {
    angle = angle - 2 * (float)M_PI;
  }
  else if (angle < -((float)M_PI)) {
    angle = 2.0f * (float)M_PI + angle;
  }

  return angle;
}

static float ResizeBetween(TransInfo *t, const float p1[3], const float p2[3])
{
  float d1[3], d2[3], len_d1;

  sub_v3_v3v3(d1, p1, t->center_global);
  sub_v3_v3v3(d2, p2, t->center_global);

  if (t->con.applyRot != NULL && (t->con.mode & CON_APPLY)) {
    mul_m3_v3(t->con.pmtx, d1);
    mul_m3_v3(t->con.pmtx, d2);
  }

  project_v3_v3v3(d1, d1, d2);

  len_d1 = len_v3(d1);

  /* Use 'invalid' dist when `center == p1` (after projecting),
   * in this case scale will _never_ move the point in relation to the center,
   * so it makes no sense to take it into account when scaling. see: T46503 */
  return len_d1 != 0.0f ? len_v3(d2) / len_d1 : TRANSFORM_DIST_INVALID;
}

/********************** CALC **************************/

static void UNUSED_FUNCTION(CalcSnapGrid)(TransInfo *t, float *UNUSED(vec))
{
  snapGridIncrementAction(t, t->tsnap.snapPoint, BIG_GEARS);
}

static void CalcSnapGeometry(TransInfo *t, float *UNUSED(vec))
{
  if (t->spacetype == SPACE_VIEW3D) {
    float loc[3];
    float no[3];
    float mval[2];
    bool found = false;
    float dist_px = SNAP_MIN_DISTANCE;  // Use a user defined value here

    mval[0] = t->mval[0];
    mval[1] = t->mval[1];

    if (t->tsnap.mode & (SCE_SNAP_MODE_VERTEX | SCE_SNAP_MODE_EDGE | SCE_SNAP_MODE_FACE |
                         SCE_SNAP_MODE_EDGE_MIDPOINT | SCE_SNAP_MODE_EDGE_PERPENDICULAR)) {
      zero_v3(no); /* objects won't set this */
      found = snapObjectsTransform(t, mval, &dist_px, loc, no);
    }
    if ((found == false) && (t->tsnap.mode & SCE_SNAP_MODE_VOLUME)) {
      found = peelObjectsTransform(
          t, mval, (t->settings->snap_flag & SCE_SNAP_PEEL_OBJECT) != 0, loc, no, NULL);
    }

    if (found == true) {
      copy_v3_v3(t->tsnap.snapPoint, loc);
      copy_v3_v3(t->tsnap.snapNormal, no);

      t->tsnap.status |= POINT_INIT;
    }
    else {
      t->tsnap.status &= ~POINT_INIT;
    }
  }
  else if (t->spacetype == SPACE_IMAGE && t->obedit_type == OB_MESH) {
    if (t->tsnap.mode & SCE_SNAP_MODE_VERTEX) {
      Image *ima = ED_space_image(t->sa->spacedata.first);
      float co[2];

      UI_view2d_region_to_view(&t->ar->v2d, t->mval[0], t->mval[1], &co[0], &co[1]);

      uint objects_len = 0;
      Object **objects = BKE_view_layer_array_from_objects_in_edit_mode_unique_data_with_uvs(
          t->view_layer, NULL, &objects_len);

      float dist_sq = FLT_MAX;
      if (ED_uvedit_nearest_uv_multi(
              t->scene, ima, objects, objects_len, co, &dist_sq, t->tsnap.snapPoint)) {
        t->tsnap.snapPoint[0] *= t->aspect[0];
        t->tsnap.snapPoint[1] *= t->aspect[1];

        t->tsnap.status |= POINT_INIT;
      }
      else {
        t->tsnap.status &= ~POINT_INIT;
      }
      MEM_freeN(objects);
    }
  }
  else if (t->spacetype == SPACE_NODE) {
    if (t->tsnap.mode & (SCE_SNAP_MODE_NODE_X | SCE_SNAP_MODE_NODE_Y)) {
      float loc[2];
      float dist_px = SNAP_MIN_DISTANCE;  // Use a user defined value here
      char node_border;

      if (snapNodesTransform(t, t->mval, loc, &dist_px, &node_border)) {
        copy_v2_v2(t->tsnap.snapPoint, loc);
        t->tsnap.snapNodeBorder = node_border;

        t->tsnap.status |= POINT_INIT;
      }
      else {
        t->tsnap.status &= ~POINT_INIT;
      }
    }
  }
}

/********************** TARGET **************************/

static void TargetSnapOffset(TransInfo *t, TransData *td)
{
  if (t->spacetype == SPACE_NODE && td != NULL) {
    bNode *node = td->extra;
    char border = t->tsnap.snapNodeBorder;
    float width = BLI_rctf_size_x(&node->totr);
    float height = BLI_rctf_size_y(&node->totr);

#ifdef USE_NODE_CENTER
    if (border & NODE_LEFT) {
      t->tsnap.snapTarget[0] -= 0.5f * width;
    }
    if (border & NODE_RIGHT) {
      t->tsnap.snapTarget[0] += 0.5f * width;
    }
    if (border & NODE_BOTTOM) {
      t->tsnap.snapTarget[1] -= 0.5f * height;
    }
    if (border & NODE_TOP) {
      t->tsnap.snapTarget[1] += 0.5f * height;
    }
#else
    if (border & NODE_LEFT) {
      t->tsnap.snapTarget[0] -= 0.0f;
    }
    if (border & NODE_RIGHT) {
      t->tsnap.snapTarget[0] += width;
    }
    if (border & NODE_BOTTOM) {
      t->tsnap.snapTarget[1] -= height;
    }
    if (border & NODE_TOP) {
      t->tsnap.snapTarget[1] += 0.0f;
    }
#endif
  }
}

static void TargetSnapCenter(TransInfo *t)
{
  /* Only need to calculate once */
  if ((t->tsnap.status & TARGET_INIT) == 0) {
    copy_v3_v3(t->tsnap.snapTarget, t->center_global);
    TargetSnapOffset(t, NULL);

    t->tsnap.status |= TARGET_INIT;
  }
}

static void TargetSnapActive(TransInfo *t)
{
  /* Only need to calculate once */
  if ((t->tsnap.status & TARGET_INIT) == 0) {
    if (calculateCenterActive(t, true, t->tsnap.snapTarget)) {
      TargetSnapOffset(t, NULL);

      t->tsnap.status |= TARGET_INIT;
    }
    /* No active, default to median */
    else {
      t->tsnap.target = SCE_SNAP_TARGET_MEDIAN;
      t->tsnap.targetSnap = TargetSnapMedian;
      TargetSnapMedian(t);
    }
  }
}

static void TargetSnapMedian(TransInfo *t)
{
  // Only need to calculate once
  if ((t->tsnap.status & TARGET_INIT) == 0) {
    int i_accum = 0;

    t->tsnap.snapTarget[0] = 0;
    t->tsnap.snapTarget[1] = 0;
    t->tsnap.snapTarget[2] = 0;

    FOREACH_TRANS_DATA_CONTAINER (t, tc) {
      TransData *td = tc->data;
      int i;
      float v[3];
      zero_v3(v);

      for (i = 0; i < tc->data_len && td->flag & TD_SELECTED; i++, td++) {
        add_v3_v3(v, td->center);
      }

      if (i == 0) {
        /* Is this possible? */
        continue;
      }

      mul_v3_fl(v, 1.0 / i);

      if (tc->use_local_mat) {
        mul_m4_v3(tc->mat, v);
      }

      add_v3_v3(t->tsnap.snapTarget, v);
      i_accum++;
    }

    mul_v3_fl(t->tsnap.snapTarget, 1.0 / i_accum);

    TargetSnapOffset(t, NULL);

    t->tsnap.status |= TARGET_INIT;
  }
}

static void TargetSnapClosest(TransInfo *t)
{
  // Only valid if a snap point has been selected
  if (t->tsnap.status & POINT_INIT) {
    float dist_closest = 0.0f;
    TransData *closest = NULL;

    /* Object mode */
    if (t->flag & T_OBJECT) {
      int i;
      FOREACH_TRANS_DATA_CONTAINER (t, tc) {
        TransData *td = tc->data;
        for (td = tc->data, i = 0; i < tc->data_len && td->flag & TD_SELECTED; i++, td++) {
          struct BoundBox *bb = BKE_object_boundbox_get(td->ob);

          /* use boundbox if possible */
          if (bb) {
            int j;

            for (j = 0; j < 8; j++) {
              float loc[3];
              float dist;

              copy_v3_v3(loc, bb->vec[j]);
              mul_m4_v3(td->ext->obmat, loc);

              dist = t->tsnap.distance(t, loc, t->tsnap.snapPoint);

              if ((dist != TRANSFORM_DIST_INVALID) &&
                  (closest == NULL || fabsf(dist) < fabsf(dist_closest))) {
                copy_v3_v3(t->tsnap.snapTarget, loc);
                closest = td;
                dist_closest = dist;
              }
            }
          }
          /* use element center otherwise */
          else {
            float loc[3];
            float dist;

            copy_v3_v3(loc, td->center);

            dist = t->tsnap.distance(t, loc, t->tsnap.snapPoint);

            if ((dist != TRANSFORM_DIST_INVALID) &&
                (closest == NULL || fabsf(dist) < fabsf(dist_closest))) {
              copy_v3_v3(t->tsnap.snapTarget, loc);
              closest = td;
            }
          }
        }
      }
    }
    else {
      FOREACH_TRANS_DATA_CONTAINER (t, tc) {
        TransData *td = tc->data;
        int i;
        for (i = 0; i < tc->data_len && td->flag & TD_SELECTED; i++, td++) {
          float loc[3];
          float dist;

          copy_v3_v3(loc, td->center);

          if (tc->use_local_mat) {
            mul_m4_v3(tc->mat, loc);
          }

          dist = t->tsnap.distance(t, loc, t->tsnap.snapPoint);

          if ((dist != TRANSFORM_DIST_INVALID) &&
              (closest == NULL || fabsf(dist) < fabsf(dist_closest))) {
            copy_v3_v3(t->tsnap.snapTarget, loc);
            closest = td;
            dist_closest = dist;
          }
        }
      }
    }

    TargetSnapOffset(t, closest);

    t->tsnap.status |= TARGET_INIT;
  }
}

bool snapObjectsTransform(
    TransInfo *t, const float mval[2], float *dist_px, float r_loc[3], float r_no[3])
{
  return ED_transform_snap_object_project_view3d(
      t->tsnap.object_context,
      t->scene->toolsettings->snap_mode,
      &(const struct SnapObjectParams){
          .snap_select = t->tsnap.modeSelect,
          .use_object_edit_cage = (t->flag & T_EDIT) != 0,
          .use_occlusion_test = t->scene->toolsettings->snap_mode != SCE_SNAP_MODE_FACE,
      },
      mval,
      t->center_global,
      dist_px,
      r_loc,
      r_no);
}

/******************** PEELING *********************************/

bool peelObjectsSnapContext(SnapObjectContext *sctx,
                            const float mval[2],
                            const struct SnapObjectParams *params,
                            const bool use_peel_object,
                            /* return args */
                            float r_loc[3],
                            float r_no[3],
                            float *r_thickness)
{
  ListBase depths_peel = {0};
  ED_transform_snap_object_project_all_view3d_ex(sctx, params, mval, -1.0f, false, &depths_peel);

  if (!BLI_listbase_is_empty(&depths_peel)) {
    /* At the moment we only use the hits of the first object */
    struct SnapObjectHitDepth *hit_min = depths_peel.first;
    for (struct SnapObjectHitDepth *iter = hit_min->next; iter; iter = iter->next) {
      if (iter->depth < hit_min->depth) {
        hit_min = iter;
      }
    }
    struct SnapObjectHitDepth *hit_max = NULL;

    if (use_peel_object) {
      /* if peeling objects, take the first and last from each object */
      hit_max = hit_min;
      for (struct SnapObjectHitDepth *iter = depths_peel.first; iter; iter = iter->next) {
        if ((iter->depth > hit_max->depth) && (iter->ob_uuid == hit_min->ob_uuid)) {
          hit_max = iter;
        }
      }
    }
    else {
      /* otherwise, pair first with second and so on */
      for (struct SnapObjectHitDepth *iter = depths_peel.first; iter; iter = iter->next) {
        if ((iter != hit_min) && (iter->ob_uuid == hit_min->ob_uuid)) {
          if (hit_max == NULL) {
            hit_max = iter;
          }
          else if (iter->depth < hit_max->depth) {
            hit_max = iter;
          }
        }
      }
      /* in this case has only one hit. treat as raycast */
      if (hit_max == NULL) {
        hit_max = hit_min;
      }
    }

    mid_v3_v3v3(r_loc, hit_min->co, hit_max->co);

    if (r_thickness) {
      *r_thickness = hit_max->depth - hit_min->depth;
    }

    /* XXX, is there a correct normal in this case ???, for now just z up */
    r_no[0] = 0.0;
    r_no[1] = 0.0;
    r_no[2] = 1.0;

    BLI_freelistN(&depths_peel);
    return true;
  }
  return false;
}

bool peelObjectsTransform(TransInfo *t,
                          const float mval[2],
                          const bool use_peel_object,
                          /* return args */
                          float r_loc[3],
                          float r_no[3],
                          float *r_thickness)
{
  return peelObjectsSnapContext(t->tsnap.object_context,
                                mval,
                                &(const struct SnapObjectParams){
                                    .snap_select = t->tsnap.modeSelect,
                                    .use_object_edit_cage = (t->flag & T_EDIT) != 0,
                                },
                                use_peel_object,
                                r_loc,
                                r_no,
                                r_thickness);
}

/******************** NODES ***********************************/

static bool snapNodeTest(View2D *v2d, bNode *node, eSnapSelect snap_select)
{
  /* node is use for snapping only if a) snap mode matches and b) node is inside the view */
  return ((snap_select == SNAP_NOT_SELECTED && !(node->flag & NODE_SELECT)) ||
          (snap_select == SNAP_ALL && !(node->flag & NODE_ACTIVE))) &&
         (node->totr.xmin < v2d->cur.xmax && node->totr.xmax > v2d->cur.xmin &&
          node->totr.ymin < v2d->cur.ymax && node->totr.ymax > v2d->cur.ymin);
}

static NodeBorder snapNodeBorder(int snap_node_mode)
{
  NodeBorder flag = 0;
  if (snap_node_mode & SCE_SNAP_MODE_NODE_X) {
    flag |= NODE_LEFT | NODE_RIGHT;
  }
  if (snap_node_mode & SCE_SNAP_MODE_NODE_Y) {
    flag |= NODE_TOP | NODE_BOTTOM;
  }
  return flag;
}

static bool snapNode(ToolSettings *ts,
                     SpaceNode *UNUSED(snode),
                     ARegion *ar,
                     bNode *node,
                     const int mval[2],
                     float r_loc[2],
                     float *r_dist_px,
                     char *r_node_border)
{
  View2D *v2d = &ar->v2d;
  NodeBorder border = snapNodeBorder(ts->snap_node_mode);
  bool retval = false;
  rcti totr;
  int new_dist;

  UI_view2d_view_to_region_rcti(v2d, &node->totr, &totr);

  if (border & NODE_LEFT) {
    new_dist = abs(totr.xmin - mval[0]);
    if (new_dist < *r_dist_px) {
      UI_view2d_region_to_view(v2d, totr.xmin, mval[1], &r_loc[0], &r_loc[1]);
      *r_dist_px = new_dist;
      *r_node_border = NODE_LEFT;
      retval = true;
    }
  }

  if (border & NODE_RIGHT) {
    new_dist = abs(totr.xmax - mval[0]);
    if (new_dist < *r_dist_px) {
      UI_view2d_region_to_view(v2d, totr.xmax, mval[1], &r_loc[0], &r_loc[1]);
      *r_dist_px = new_dist;
      *r_node_border = NODE_RIGHT;
      retval = true;
    }
  }

  if (border & NODE_BOTTOM) {
    new_dist = abs(totr.ymin - mval[1]);
    if (new_dist < *r_dist_px) {
      UI_view2d_region_to_view(v2d, mval[0], totr.ymin, &r_loc[0], &r_loc[1]);
      *r_dist_px = new_dist;
      *r_node_border = NODE_BOTTOM;
      retval = true;
    }
  }

  if (border & NODE_TOP) {
    new_dist = abs(totr.ymax - mval[1]);
    if (new_dist < *r_dist_px) {
      UI_view2d_region_to_view(v2d, mval[0], totr.ymax, &r_loc[0], &r_loc[1]);
      *r_dist_px = new_dist;
      *r_node_border = NODE_TOP;
      retval = true;
    }
  }

  return retval;
}

static bool snapNodes(ToolSettings *ts,
                      SpaceNode *snode,
                      ARegion *ar,
                      const int mval[2],
                      eSnapSelect snap_select,
                      float r_loc[2],
                      float *r_dist_px,
                      char *r_node_border)
{
  bNodeTree *ntree = snode->edittree;
  bNode *node;
  bool retval = false;

  *r_node_border = 0;

  for (node = ntree->nodes.first; node; node = node->next) {
    if (snapNodeTest(&ar->v2d, node, snap_select)) {
      retval |= snapNode(ts, snode, ar, node, mval, r_loc, r_dist_px, r_node_border);
    }
  }

  return retval;
}

bool snapNodesTransform(
    TransInfo *t, const int mval[2], float r_loc[2], float *r_dist_px, char *r_node_border)
{
  return snapNodes(t->settings,
                   t->sa->spacedata.first,
                   t->ar,
                   mval,
                   t->tsnap.modeSelect,
                   r_loc,
                   r_dist_px,
                   r_node_border);
}

/*================================================================*/

static void applyGridIncrement(
    TransInfo *t, float *val, int max_index, const float fac[3], GearsType action);

void snapGridIncrementAction(TransInfo *t, float *val, GearsType action)
{
  float fac[3];

  fac[NO_GEARS] = t->snap[0];
  fac[BIG_GEARS] = t->snap[1];
  fac[SMALL_GEARS] = t->snap[2];

  applyGridIncrement(t, val, t->idx_max, fac, action);
}

void snapGridIncrement(TransInfo *t, float *val)
{
  GearsType action;

  /* only do something if using absolute or incremental grid snapping
   * and there is no valid snap point */
  if ((!(t->tsnap.mode & (SCE_SNAP_MODE_INCREMENT | SCE_SNAP_MODE_GRID)) || validSnap(t)) &&
      !doForceIncrementSnap(t)) {
    return;
  }

  action = activeSnap(t) ? BIG_GEARS : NO_GEARS;

  if (action == BIG_GEARS && (t->modifiers & MOD_PRECISION)) {
    action = SMALL_GEARS;
  }

  snapGridIncrementAction(t, val, action);
}

void snapSequenceBounds(TransInfo *t, const int mval[2])
{
  float xmouse, ymouse;
  int frame;
  int mframe;
  TransSeq *ts = TRANS_DATA_CONTAINER_FIRST_SINGLE(t)->custom.type.data;
  /* reuse increment, strictly speaking could be another snap mode, but leave as is */
  if (!(t->modifiers & MOD_SNAP_INVERT)) {
    return;
  }

  /* convert to frame range */
  UI_view2d_region_to_view(&t->ar->v2d, mval[0], mval[1], &xmouse, &ymouse);
  mframe = round_fl_to_int(xmouse);
  /* now find the closest sequence */
  frame = BKE_sequencer_find_next_prev_edit(t->scene, mframe, SEQ_SIDE_BOTH, true, false, true);

  if (!ts->snap_left) {
    frame = frame - (ts->max - ts->min);
  }

  t->values[0] = frame - ts->min;
}

static void applyGridIncrement(
    TransInfo *t, float *val, int max_index, const float fac[3], GearsType action)
{
  float asp_local[3] = {1, 1, 1};
  const bool use_aspect = ELEM(t->mode, TFM_TRANSLATION);
  const float *asp = use_aspect ? t->aspect : asp_local;
  int i;

  BLI_assert((t->tsnap.mode & (SCE_SNAP_MODE_INCREMENT | SCE_SNAP_MODE_GRID)) ||
             doForceIncrementSnap(t));
  BLI_assert(max_index <= 2);

  /* Early bailing out if no need to snap */
  if (fac[action] == 0.0f) {
    return;
  }

  if (use_aspect) {
    /* custom aspect for fcurve */
    if (t->spacetype == SPACE_GRAPH) {
      View2D *v2d = &t->ar->v2d;
      Scene *scene = t->scene;
      SpaceGraph *sipo = t->sa->spacedata.first;
      asp_local[0] = UI_view2d_grid_resolution_x__frames_or_seconds(
          v2d, scene, sipo->flag & SIPO_DRAWTIME);
      asp_local[1] = UI_view2d_grid_resolution_y__values(v2d);
      asp = asp_local;
    }
  }

  /* absolute snapping on grid based on global center */
  if ((t->tsnap.snap_spatial_grid) && (t->mode == TFM_TRANSLATION)) {
    const float *center_global = t->center_global;
    bool use_local_axis = false;

    /* use a fallback for cursor selection,
     * this isn't useful as a global center for absolute grid snapping
     * since its not based on the position of the selection. */
    if (t->around == V3D_AROUND_CURSOR) {
      const TransCenterData *cd = transformCenter_from_type(t, V3D_AROUND_CENTER_MEDIAN);
      center_global = cd->global;
    }

    if (t->con.mode & (CON_AXIS0 | CON_AXIS1 | CON_AXIS2)) {
      use_local_axis = true;
    }

    for (i = 0; i <= max_index; i++) {
      /* do not let unconstrained axis jump to absolute grid increments */
      if (!(t->con.mode & CON_APPLY) || t->con.mode & (CON_AXIS0 << i)) {
        const float iter_fac = fac[action] * asp[i];

        if (use_local_axis) {
          float local_axis[3];
          float pos_on_axis[3];

          copy_v3_v3(local_axis, t->con.mtx[i]);
          copy_v3_v3(pos_on_axis, t->con.mtx[i]);

          /* amount of movement on axis from initial pos */
          mul_v3_fl(pos_on_axis, val[i]);

          /* actual global position on axis */
          add_v3_v3(pos_on_axis, center_global);

          float min_dist = INFINITY;
          for (int j = 0; j < 3; j++) {
            if (fabs(local_axis[j]) < 0.01f) {
              /* Ignore very small (normalized) axis changes */
              continue;
            }

            /* closest point on grid */
            float grid_p = iter_fac * roundf(pos_on_axis[j] / iter_fac);
            float dist_p = fabs((grid_p - pos_on_axis[j]) / local_axis[j]);

            /* The amount of distance needed to travel along the
             * local axis to snap to the closest grid point */
            /* in the global j axis direction */
            float move_dist = (grid_p - center_global[j]) / local_axis[j];

            if (dist_p < min_dist) {
              min_dist = dist_p;
              val[i] = move_dist;
            }
          }
        }
        else {
          val[i] = iter_fac * roundf((val[i] + center_global[i]) / iter_fac) - center_global[i];
        }
      }
    }
  }
  else {
    /* relative snapping in fixed increments */
    for (i = 0; i <= max_index; i++) {
      const float iter_fac = fac[action] * asp[i];
      val[i] = iter_fac * roundf(val[i] / iter_fac);
    }
  }
}
