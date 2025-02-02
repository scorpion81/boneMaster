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
 */

#ifndef __BKE_EDITMESH_H__
#define __BKE_EDITMESH_H__

/** \file
 * \ingroup bke
 *
 * The \link edmesh EDBM module\endlink is for editmode bmesh stuff.
 * In contrast, this module is for code shared with blenkernel that's
 * only concerned with low level operations on the #BMEditMesh structure.
 */

#include "BKE_customdata.h"
#include "bmesh.h"

struct BMLoop;
struct BMesh;
struct Depsgraph;
struct DerivedMesh;
struct EditMeshData;
struct Mesh;
struct MeshStatVis;
struct Scene;

/**
 * This structure is used for mesh edit-mode.
 *
 * through this, you get access to both the edit #BMesh,
 * it's tessellation, and various stuff that doesn't belong in the BMesh
 * struct itself.
 *
 * the entire derivedmesh and modifier system works with this structure,
 * and not BMesh.  Mesh->edit_bmesh stores a pointer to this structure. */
typedef struct BMEditMesh {
  struct BMesh *bm;

  /*this is for undoing failed operations*/
  struct BMEditMesh *emcopy;
  int emcopyusers;

  /* we store tessellations as triplets of three loops,
   * which each define a triangle.*/
  struct BMLoop *(*looptris)[3];
  int tottri;

  struct Mesh *mesh_eval_final, *mesh_eval_cage;

  /*derivedmesh stuff*/
  CustomData_MeshMasks lastDataMask;
  unsigned char (*derivedVertColor)[4];
  int derivedVertColorLen;
  unsigned char (*derivedFaceColor)[4];
  int derivedFaceColorLen;

  /*selection mode*/
  short selectmode;
  short mat_nr;

  /* Object this editmesh came from (if it came from one) */
  struct Object *ob;

  /*temp variables for x-mirror editing*/
  int mirror_cdlayer; /* -1 is invalid */
} BMEditMesh;

/* editmesh.c */
void BKE_editmesh_tessface_calc(BMEditMesh *em);
BMEditMesh *BKE_editmesh_create(BMesh *bm, const bool do_tessellate);
BMEditMesh *BKE_editmesh_copy(BMEditMesh *em);
BMEditMesh *BKE_editmesh_from_object(struct Object *ob);
void BKE_editmesh_free_derivedmesh(BMEditMesh *em);
void BKE_editmesh_free(BMEditMesh *em);

void BKE_editmesh_color_free(BMEditMesh *em);
void BKE_editmesh_color_ensure(BMEditMesh *em, const char htype);
float (*BKE_editmesh_vert_coords_alloc_orco(BMEditMesh *em, int *r_vert_len))[3];
void BKE_editmesh_lnorspace_update(BMEditMesh *em);
void BKE_editmesh_ensure_autosmooth(BMEditMesh *em);

/* editderivedmesh.c */
/* should really be defined in editmesh.c, but they use 'EditDerivedBMesh' */
void BKE_editmesh_statvis_calc(BMEditMesh *em,
                               struct EditMeshData *emd,
                               const struct MeshStatVis *statvis);

float (*BKE_editmesh_vert_coords_alloc(
    struct Depsgraph *depsgraph, struct BMEditMesh *em, struct Scene *scene, int *r_vert_len))[3];

#endif /* __BKE_EDITMESH_H__ */
