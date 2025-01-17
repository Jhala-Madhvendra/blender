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
 * The Original Code is Copyright (C) 2014 Blender Foundation.
 * All rights reserved.
 */

/** \file
 * \ingroup edgizmolib
 *
 * \name Cage Gizmo
 *
 * 2D Gizmo
 *
 * \brief Rectangular gizmo acting as a 'cage' around its content.
 * Interacting scales or translates the gizmo.
 */

#include "MEM_guardedalloc.h"

#include "BLI_dial_2d.h"
#include "BLI_math.h"
#include "BLI_rect.h"

#include "BKE_context.h"

#include "GPU_immediate.h"
#include "GPU_immediate_util.h"
#include "GPU_matrix.h"
#include "GPU_select.h"
#include "GPU_shader.h"
#include "GPU_state.h"

#include "RNA_access.h"
#include "RNA_define.h"

#include "WM_api.h"
#include "WM_types.h"

#include "ED_gizmo_library.h"
#include "ED_screen.h"
#include "ED_view3d.h"

/* own includes */
#include "../gizmo_library_intern.h"

#define GIZMO_MARGIN_OFFSET_SCALE 1.5f

static bool gizmo_calc_rect_view_scale(const wmGizmo *gz, const float dims[2], float scale[2])
{
  float matrix_final_no_offset[4][4];
  float asp[2] = {1.0f, 1.0f};
  if (dims[0] > dims[1]) {
    asp[0] = dims[1] / dims[0];
  }
  else {
    asp[1] = dims[0] / dims[1];
  }
  float x_axis[3], y_axis[3];
  WM_gizmo_calc_matrix_final_no_offset(gz, matrix_final_no_offset);
  mul_v3_mat3_m4v3(x_axis, matrix_final_no_offset, gz->matrix_offset[0]);
  mul_v3_mat3_m4v3(y_axis, matrix_final_no_offset, gz->matrix_offset[1]);

  mul_v2_v2(x_axis, asp);
  mul_v2_v2(y_axis, asp);

  float len_x_axis = len_v3(x_axis);
  float len_y_axis = len_v3(y_axis);

  if (len_x_axis == 0.0f || len_y_axis == 0.0f) {
    return false;
  }

  scale[0] = 1.0f / len_x_axis;
  scale[1] = 1.0f / len_y_axis;
  return true;
}

static bool gizmo_calc_rect_view_margin(const wmGizmo *gz, const float dims[2], float margin[2])
{
  float handle_size;
  handle_size = 0.15f;
  handle_size *= gz->scale_final;
  float scale_xy[2];
  if (!gizmo_calc_rect_view_scale(gz, dims, scale_xy)) {
    zero_v2(margin);
    return false;
  }

  margin[0] = ((handle_size * scale_xy[0]));
  margin[1] = ((handle_size * scale_xy[1]));
  return true;
}

/* -------------------------------------------------------------------- */
/** \name Box Draw Style
 *
 * Useful for 3D views, see: #ED_GIZMO_CAGE2D_STYLE_BOX
 * \{ */

static void cage2d_draw_box_corners(const rctf *r,
                                    const float margin[2],
                                    const float color[3],
                                    const float line_width)
{
  uint pos = GPU_vertformat_attr_add(immVertexFormat(), "pos", GPU_COMP_F32, 2, GPU_FETCH_FLOAT);

  immBindBuiltinProgram(GPU_SHADER_3D_POLYLINE_UNIFORM_COLOR);
  immUniformColor3fv(color);

  float viewport[4];
  GPU_viewport_size_get_f(viewport);
  immUniform2fv("viewportSize", &viewport[2]);

  immUniform1f("lineWidth", line_width * U.pixelsize);

  immBegin(GPU_PRIM_LINES, 16);

  immVertex2f(pos, r->xmin, r->ymin + margin[1]);
  immVertex2f(pos, r->xmin, r->ymin);
  immVertex2f(pos, r->xmin, r->ymin);
  immVertex2f(pos, r->xmin + margin[0], r->ymin);

  immVertex2f(pos, r->xmax, r->ymin + margin[1]);
  immVertex2f(pos, r->xmax, r->ymin);
  immVertex2f(pos, r->xmax, r->ymin);
  immVertex2f(pos, r->xmax - margin[0], r->ymin);

  immVertex2f(pos, r->xmax, r->ymax - margin[1]);
  immVertex2f(pos, r->xmax, r->ymax);
  immVertex2f(pos, r->xmax, r->ymax);
  immVertex2f(pos, r->xmax - margin[0], r->ymax);

  immVertex2f(pos, r->xmin, r->ymax - margin[1]);
  immVertex2f(pos, r->xmin, r->ymax);
  immVertex2f(pos, r->xmin, r->ymax);
  immVertex2f(pos, r->xmin + margin[0], r->ymax);

  immEnd();

  immUnbindProgram();
}

static void cage2d_draw_box_interaction(const float color[4],
                                        const int highlighted,
                                        const float size[2],
                                        const float margin[2],
                                        const float line_width,
                                        const bool is_solid,
                                        const int draw_options)
{
  /* 4 verts for translate, otherwise only 3 are used. */
  float verts[4][2];
  uint verts_len = 0;
  GPUPrimType prim_type = GPU_PRIM_NONE;

  switch (highlighted) {
    case ED_GIZMO_CAGE2D_PART_SCALE_MIN_X: {
      rctf r = {
          .xmin = -size[0],
          .xmax = -size[0] + margin[0],
          .ymin = -size[1] + margin[1],
          .ymax = size[1] - margin[1],
      };
      ARRAY_SET_ITEMS(verts[0], r.xmin, r.ymin);
      ARRAY_SET_ITEMS(verts[1], r.xmin, r.ymax);
      verts_len = 2;
      if (is_solid) {
        ARRAY_SET_ITEMS(verts[2], r.xmax, r.ymax);
        ARRAY_SET_ITEMS(verts[3], r.xmax, r.ymin);
        verts_len += 2;
        prim_type = GPU_PRIM_TRI_FAN;
      }
      else {
        prim_type = GPU_PRIM_LINE_STRIP;
      }
      break;
    }
    case ED_GIZMO_CAGE2D_PART_SCALE_MAX_X: {
      rctf r = {
          .xmin = size[0] - margin[0],
          .xmax = size[0],
          .ymin = -size[1] + margin[1],
          .ymax = size[1] - margin[1],
      };
      ARRAY_SET_ITEMS(verts[0], r.xmax, r.ymin);
      ARRAY_SET_ITEMS(verts[1], r.xmax, r.ymax);
      verts_len = 2;
      if (is_solid) {
        ARRAY_SET_ITEMS(verts[2], r.xmin, r.ymax);
        ARRAY_SET_ITEMS(verts[3], r.xmin, r.ymin);
        verts_len += 2;
        prim_type = GPU_PRIM_TRI_FAN;
      }
      else {
        prim_type = GPU_PRIM_LINE_STRIP;
      }
      break;
    }
    case ED_GIZMO_CAGE2D_PART_SCALE_MIN_Y: {
      rctf r = {
          .xmin = -size[0] + margin[0],
          .xmax = size[0] - margin[0],
          .ymin = -size[1],
          .ymax = -size[1] + margin[1],
      };
      ARRAY_SET_ITEMS(verts[0], r.xmin, r.ymin);
      ARRAY_SET_ITEMS(verts[1], r.xmax, r.ymin);
      verts_len = 2;
      if (is_solid) {
        ARRAY_SET_ITEMS(verts[2], r.xmax, r.ymax);
        ARRAY_SET_ITEMS(verts[3], r.xmin, r.ymax);
        verts_len += 2;
        prim_type = GPU_PRIM_TRI_FAN;
      }
      else {
        prim_type = GPU_PRIM_LINE_STRIP;
      }
      break;
    }
    case ED_GIZMO_CAGE2D_PART_SCALE_MAX_Y: {
      rctf r = {
          .xmin = -size[0] + margin[0],
          .xmax = size[0] - margin[0],
          .ymin = size[1] - margin[1],
          .ymax = size[1],
      };
      ARRAY_SET_ITEMS(verts[0], r.xmin, r.ymax);
      ARRAY_SET_ITEMS(verts[1], r.xmax, r.ymax);
      verts_len = 2;
      if (is_solid) {
        ARRAY_SET_ITEMS(verts[2], r.xmax, r.ymin);
        ARRAY_SET_ITEMS(verts[3], r.xmin, r.ymin);
        verts_len += 2;
        prim_type = GPU_PRIM_TRI_FAN;
      }
      else {
        prim_type = GPU_PRIM_LINE_STRIP;
      }
      break;
    }
    case ED_GIZMO_CAGE2D_PART_SCALE_MIN_X_MIN_Y: {
      rctf r = {
          .xmin = -size[0],
          .xmax = -size[0] + margin[0],
          .ymin = -size[1],
          .ymax = -size[1] + margin[1],
      };
      ARRAY_SET_ITEMS(verts[0], r.xmax, r.ymin);
      ARRAY_SET_ITEMS(verts[1], r.xmax, r.ymax);
      ARRAY_SET_ITEMS(verts[2], r.xmin, r.ymax);
      verts_len = 3;
      if (is_solid) {
        ARRAY_SET_ITEMS(verts[3], r.xmin, r.ymin);
        verts_len += 1;
        prim_type = GPU_PRIM_TRI_FAN;
      }
      else {
        prim_type = GPU_PRIM_LINE_STRIP;
      }
      break;
    }
    case ED_GIZMO_CAGE2D_PART_SCALE_MIN_X_MAX_Y: {
      rctf r = {
          .xmin = -size[0],
          .xmax = -size[0] + margin[0],
          .ymin = size[1] - margin[1],
          .ymax = size[1],
      };
      ARRAY_SET_ITEMS(verts[0], r.xmax, r.ymax);
      ARRAY_SET_ITEMS(verts[1], r.xmax, r.ymin);
      ARRAY_SET_ITEMS(verts[2], r.xmin, r.ymin);
      verts_len = 3;
      if (is_solid) {
        ARRAY_SET_ITEMS(verts[3], r.xmin, r.ymax);
        verts_len += 1;
        prim_type = GPU_PRIM_TRI_FAN;
      }
      else {
        prim_type = GPU_PRIM_LINE_STRIP;
      }
      break;
    }
    case ED_GIZMO_CAGE2D_PART_SCALE_MAX_X_MIN_Y: {
      rctf r = {
          .xmin = size[0] - margin[0],
          .xmax = size[0],
          .ymin = -size[1],
          .ymax = -size[1] + margin[1],
      };
      ARRAY_SET_ITEMS(verts[0], r.xmin, r.ymin);
      ARRAY_SET_ITEMS(verts[1], r.xmin, r.ymax);
      ARRAY_SET_ITEMS(verts[2], r.xmax, r.ymax);
      verts_len = 3;
      if (is_solid) {
        ARRAY_SET_ITEMS(verts[3], r.xmax, r.ymin);
        verts_len += 1;
        prim_type = GPU_PRIM_TRI_FAN;
      }
      else {
        prim_type = GPU_PRIM_LINE_STRIP;
      }
      break;
    }
    case ED_GIZMO_CAGE2D_PART_SCALE_MAX_X_MAX_Y: {
      rctf r = {
          .xmin = size[0] - margin[0],
          .xmax = size[0],
          .ymin = size[1] - margin[1],
          .ymax = size[1],
      };
      ARRAY_SET_ITEMS(verts[0], r.xmin, r.ymax);
      ARRAY_SET_ITEMS(verts[1], r.xmin, r.ymin);
      ARRAY_SET_ITEMS(verts[2], r.xmax, r.ymin);
      verts_len = 3;
      if (is_solid) {
        ARRAY_SET_ITEMS(verts[3], r.xmax, r.ymax);
        verts_len += 1;
        prim_type = GPU_PRIM_TRI_FAN;
      }
      else {
        prim_type = GPU_PRIM_LINE_STRIP;
      }
      break;
    }
    case ED_GIZMO_CAGE2D_PART_ROTATE: {
      const float rotate_pt[2] = {0.0f, size[1] + margin[1]};
      const rctf r_rotate = {
          .xmin = rotate_pt[0] - margin[0] / 2.0f,
          .xmax = rotate_pt[0] + margin[0] / 2.0f,
          .ymin = rotate_pt[1] - margin[1] / 2.0f,
          .ymax = rotate_pt[1] + margin[1] / 2.0f,
      };

      ARRAY_SET_ITEMS(verts[0], r_rotate.xmin, r_rotate.ymin);
      ARRAY_SET_ITEMS(verts[1], r_rotate.xmin, r_rotate.ymax);
      ARRAY_SET_ITEMS(verts[2], r_rotate.xmax, r_rotate.ymax);
      ARRAY_SET_ITEMS(verts[3], r_rotate.xmax, r_rotate.ymin);

      verts_len = 4;
      if (is_solid) {
        prim_type = GPU_PRIM_TRI_FAN;
      }
      else {
        prim_type = GPU_PRIM_LINE_STRIP;
      }
      break;
    }

    case ED_GIZMO_CAGE2D_PART_TRANSLATE:
      if (draw_options & ED_GIZMO_CAGE2D_DRAW_FLAG_XFORM_CENTER_HANDLE) {
        ARRAY_SET_ITEMS(verts[0], -margin[0] / 2, -margin[1] / 2);
        ARRAY_SET_ITEMS(verts[1], margin[0] / 2, margin[1] / 2);
        ARRAY_SET_ITEMS(verts[2], -margin[0] / 2, margin[1] / 2);
        ARRAY_SET_ITEMS(verts[3], margin[0] / 2, -margin[1] / 2);
        verts_len = 4;
        if (is_solid) {
          prim_type = GPU_PRIM_TRI_FAN;
        }
        else {
          prim_type = GPU_PRIM_LINES;
        }
      }
      else {
        /* Only used for 3D view selection, never displayed to the user. */
        ARRAY_SET_ITEMS(verts[0], -size[0], -size[1]);
        ARRAY_SET_ITEMS(verts[1], -size[0], size[1]);
        ARRAY_SET_ITEMS(verts[2], size[0], size[1]);
        ARRAY_SET_ITEMS(verts[3], size[0], -size[1]);
        verts_len = 4;
        if (is_solid) {
          prim_type = GPU_PRIM_TRI_FAN;
        }
        else {
          /* unreachable */
          BLI_assert(0);
          prim_type = GPU_PRIM_LINE_STRIP;
        }
      }
      break;
    default:
      return;
  }

  BLI_assert(prim_type != GPU_PRIM_NONE);

  GPUVertFormat *format = immVertexFormat();
  struct {
    uint pos, col;
  } attr_id = {
      .pos = GPU_vertformat_attr_add(format, "pos", GPU_COMP_F32, 2, GPU_FETCH_FLOAT),
      .col = GPU_vertformat_attr_add(format, "color", GPU_COMP_F32, 3, GPU_FETCH_FLOAT),
  };
  immBindBuiltinProgram(is_solid ? GPU_SHADER_2D_FLAT_COLOR : GPU_SHADER_3D_POLYLINE_FLAT_COLOR);

  {
    if (is_solid) {
      BLI_assert(ELEM(prim_type, GPU_PRIM_TRI_FAN));
      immBegin(prim_type, verts_len);
      immAttr3f(attr_id.col, 0.0f, 0.0f, 0.0f);
      for (uint i = 0; i < verts_len; i++) {
        immVertex2fv(attr_id.pos, verts[i]);
      }
      immEnd();
    }
    else {
      BLI_assert(ELEM(prim_type, GPU_PRIM_LINE_STRIP, GPU_PRIM_LINES));

      float viewport[4];
      GPU_viewport_size_get_f(viewport);
      immUniform2fv("viewportSize", &viewport[2]);

      immUniform1f("lineWidth", (line_width * 3.0f) * U.pixelsize);

      immBegin(prim_type, verts_len);
      immAttr3f(attr_id.col, 0.0f, 0.0f, 0.0f);
      for (uint i = 0; i < verts_len; i++) {
        immVertex2fv(attr_id.pos, verts[i]);
      }
      immEnd();

      immUniform1f("lineWidth", line_width * U.pixelsize);

      immBegin(prim_type, verts_len);
      immAttr3fv(attr_id.col, color);
      for (uint i = 0; i < verts_len; i++) {
        immVertex2fv(attr_id.pos, verts[i]);
      }
      immEnd();
    }
  }

  immUnbindProgram();
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name Circle Draw Style
 *
 * Useful for 2D views, see: #ED_GIZMO_CAGE2D_STYLE_CIRCLE
 * \{ */

static void imm_draw_point_aspect_2d(
    uint pos, float x, float y, float rad_x, float rad_y, bool solid)
{
  immBegin(solid ? GPU_PRIM_TRI_FAN : GPU_PRIM_LINE_LOOP, 4);
  immVertex2f(pos, x - rad_x, y - rad_y);
  immVertex2f(pos, x - rad_x, y + rad_y);
  immVertex2f(pos, x + rad_x, y + rad_y);
  immVertex2f(pos, x + rad_x, y - rad_y);
  immEnd();
}

static void cage2d_draw_circle_wire(const rctf *r,
                                    const float margin[2],
                                    const float color[3],
                                    const int transform_flag,
                                    const int draw_options,
                                    const float line_width)
{
  uint pos = GPU_vertformat_attr_add(immVertexFormat(), "pos", GPU_COMP_F32, 2, GPU_FETCH_FLOAT);

  immBindBuiltinProgram(GPU_SHADER_3D_POLYLINE_UNIFORM_COLOR);
  immUniformColor3fv(color);

  float viewport[4];
  GPU_viewport_size_get_f(viewport);
  immUniform2fv("viewportSize", &viewport[2]);
  immUniform1f("lineWidth", line_width * U.pixelsize);

  immBegin(GPU_PRIM_LINE_LOOP, 4);
  immVertex2f(pos, r->xmin, r->ymin);
  immVertex2f(pos, r->xmax, r->ymin);
  immVertex2f(pos, r->xmax, r->ymax);
  immVertex2f(pos, r->xmin, r->ymax);
  immEnd();

  if (transform_flag & ED_GIZMO_CAGE2D_XFORM_FLAG_ROTATE) {
    immBegin(GPU_PRIM_LINE_LOOP, 2);
    immVertex2f(pos, BLI_rctf_cent_x(r), r->ymax);
    immVertex2f(pos, BLI_rctf_cent_x(r), r->ymax + margin[1]);
    immEnd();
  }

  if (transform_flag & ED_GIZMO_CAGE2D_XFORM_FLAG_TRANSLATE) {
    if (draw_options & ED_GIZMO_CAGE2D_DRAW_FLAG_XFORM_CENTER_HANDLE) {
      const float rad[2] = {margin[0] / 2, margin[1] / 2};
      const float center[2] = {BLI_rctf_cent_x(r), BLI_rctf_cent_y(r)};

      immBegin(GPU_PRIM_LINES, 4);
      immVertex2f(pos, center[0] - rad[0], center[1] - rad[1]);
      immVertex2f(pos, center[0] + rad[0], center[1] + rad[1]);
      immVertex2f(pos, center[0] + rad[0], center[1] - rad[1]);
      immVertex2f(pos, center[0] - rad[0], center[1] + rad[1]);
      immEnd();
    }
  }

  immUnbindProgram();
}

static void cage2d_draw_circle_handles(const rctf *r,
                                       const float margin[2],
                                       const float color[3],
                                       const int transform_flag,
                                       bool solid)
{
  uint pos = GPU_vertformat_attr_add(immVertexFormat(), "pos", GPU_COMP_F32, 2, GPU_FETCH_FLOAT);
  void (*circle_fn)(uint, float, float, float, float, int) = (solid) ?
                                                                 imm_draw_circle_fill_aspect_2d :
                                                                 imm_draw_circle_wire_aspect_2d;
  const int resolu = 12;
  const float rad[2] = {margin[0] / 3, margin[1] / 3};

  immBindBuiltinProgram(GPU_SHADER_2D_UNIFORM_COLOR);
  immUniformColor3fv(color);

  /* should  really divide by two, but looks too bulky. */
  {
    imm_draw_point_aspect_2d(pos, r->xmin, r->ymin, rad[0], rad[1], solid);
    imm_draw_point_aspect_2d(pos, r->xmax, r->ymin, rad[0], rad[1], solid);
    imm_draw_point_aspect_2d(pos, r->xmax, r->ymax, rad[0], rad[1], solid);
    imm_draw_point_aspect_2d(pos, r->xmin, r->ymax, rad[0], rad[1], solid);
  }

  if (transform_flag & ED_GIZMO_CAGE2D_XFORM_FLAG_ROTATE) {
    const float handle[2] = {
        BLI_rctf_cent_x(r),
        r->ymax + (margin[1] * GIZMO_MARGIN_OFFSET_SCALE),
    };
    circle_fn(pos, handle[0], handle[1], rad[0], rad[1], resolu);
  }

  immUnbindProgram();
}

/** \} */

static void gizmo_cage2d_draw_intern(wmGizmo *gz,
                                     const bool select,
                                     const bool highlight,
                                     const int select_id)
{
  // const bool use_clamp = (gz->parent_gzgroup->type->flag & WM_GIZMOGROUPTYPE_3D) == 0;
  float dims[2];
  RNA_float_get_array(gz->ptr, "dimensions", dims);
  float matrix_final[4][4];

  const int transform_flag = RNA_enum_get(gz->ptr, "transform");
  const int draw_style = RNA_enum_get(gz->ptr, "draw_style");
  const int draw_options = RNA_enum_get(gz->ptr, "draw_options");

  const float size_real[2] = {dims[0] / 2.0f, dims[1] / 2.0f};

  WM_gizmo_calc_matrix_final(gz, matrix_final);

  GPU_matrix_push();
  GPU_matrix_mul(matrix_final);

  float margin[2];
  gizmo_calc_rect_view_margin(gz, dims, margin);

  /* Handy for quick testing draw (if it's outside bounds). */
  if (false) {
    GPU_blend(GPU_BLEND_ALPHA);
    uint pos = GPU_vertformat_attr_add(immVertexFormat(), "pos", GPU_COMP_F32, 2, GPU_FETCH_FLOAT);
    immBindBuiltinProgram(GPU_SHADER_2D_UNIFORM_COLOR);
    immUniformColor4fv((const float[4]){1, 1, 1, 0.5f});
    float s = 0.5f;
    immRectf(pos, -s, -s, s, s);
    immUnbindProgram();
    GPU_blend(GPU_BLEND_NONE);
  }

  if (select) {
    /* Expand for hot-spot. */
    const float size[2] = {size_real[0] + margin[0] / 2, size_real[1] + margin[1] / 2};

    if (transform_flag & ED_GIZMO_CAGE2D_XFORM_FLAG_SCALE) {
      int scale_parts[] = {
          ED_GIZMO_CAGE2D_PART_SCALE_MIN_X,
          ED_GIZMO_CAGE2D_PART_SCALE_MAX_X,
          ED_GIZMO_CAGE2D_PART_SCALE_MIN_Y,
          ED_GIZMO_CAGE2D_PART_SCALE_MAX_Y,

          ED_GIZMO_CAGE2D_PART_SCALE_MIN_X_MIN_Y,
          ED_GIZMO_CAGE2D_PART_SCALE_MIN_X_MAX_Y,
          ED_GIZMO_CAGE2D_PART_SCALE_MAX_X_MIN_Y,
          ED_GIZMO_CAGE2D_PART_SCALE_MAX_X_MAX_Y,
      };
      for (int i = 0; i < ARRAY_SIZE(scale_parts); i++) {
        GPU_select_load_id(select_id | scale_parts[i]);
        cage2d_draw_box_interaction(
            gz->color, scale_parts[i], size, margin, gz->line_width, true, draw_options);
      }
    }
    if (transform_flag & ED_GIZMO_CAGE2D_XFORM_FLAG_TRANSLATE) {
      const int transform_part = ED_GIZMO_CAGE2D_PART_TRANSLATE;
      GPU_select_load_id(select_id | transform_part);
      cage2d_draw_box_interaction(
          gz->color, transform_part, size, margin, gz->line_width, true, draw_options);
    }
    if (transform_flag & ED_GIZMO_CAGE2D_XFORM_FLAG_ROTATE) {
      cage2d_draw_box_interaction(gz->color,
                                  ED_GIZMO_CAGE2D_PART_ROTATE,
                                  size_real,
                                  margin,
                                  gz->line_width,
                                  true,
                                  draw_options);
    }
  }
  else {
    const rctf r = {
        .xmin = -size_real[0],
        .ymin = -size_real[1],
        .xmax = size_real[0],
        .ymax = size_real[1],
    };
    if (draw_style == ED_GIZMO_CAGE2D_STYLE_BOX) {
      float color[4], black[3] = {0, 0, 0};
      gizmo_color_get(gz, highlight, color);

      /* corner gizmos */
      cage2d_draw_box_corners(&r, margin, black, gz->line_width + 3.0f);

      /* corner gizmos */
      cage2d_draw_box_corners(&r, margin, color, gz->line_width);

      bool show = false;
      if (gz->highlight_part == ED_GIZMO_CAGE2D_PART_TRANSLATE) {
        /* Only show if we're drawing the center handle
         * otherwise the entire rectangle is the hot-spot. */
        if (draw_options & ED_GIZMO_CAGE2D_DRAW_FLAG_XFORM_CENTER_HANDLE) {
          show = true;
        }
      }
      else {
        show = true;
      }

      if (show) {
        cage2d_draw_box_interaction(
            gz->color, gz->highlight_part, size_real, margin, gz->line_width, false, draw_options);
      }

      if (transform_flag & ED_GIZMO_CAGE2D_XFORM_FLAG_ROTATE) {
        cage2d_draw_box_interaction(gz->color,
                                    ED_GIZMO_CAGE2D_PART_ROTATE,
                                    size_real,
                                    margin,
                                    gz->line_width,
                                    false,
                                    draw_options);
      }
    }
    else if (draw_style == ED_GIZMO_CAGE2D_STYLE_CIRCLE) {
      float color[4], black[3] = {0, 0, 0};
      gizmo_color_get(gz, highlight, color);

      GPU_blend(GPU_BLEND_ALPHA);

      float outline_line_width = gz->line_width + 3.0f;
      cage2d_draw_circle_wire(&r, margin, black, transform_flag, draw_options, outline_line_width);
      cage2d_draw_circle_wire(&r, margin, color, transform_flag, draw_options, gz->line_width);

      /* corner gizmos */
      cage2d_draw_circle_handles(&r, margin, color, transform_flag, true);
      cage2d_draw_circle_handles(&r, margin, (const float[3]){0, 0, 0}, transform_flag, false);

      GPU_blend(GPU_BLEND_NONE);
    }
    else {
      BLI_assert(0);
    }
  }

  GPU_matrix_pop();
}

/**
 * For when we want to draw 2d cage in 3d views.
 */
static void gizmo_cage2d_draw_select(const bContext *UNUSED(C), wmGizmo *gz, int select_id)
{
  gizmo_cage2d_draw_intern(gz, true, false, select_id);
}

static void gizmo_cage2d_draw(const bContext *UNUSED(C), wmGizmo *gz)
{
  const bool is_highlight = (gz->state & WM_GIZMO_STATE_HIGHLIGHT) != 0;
  gizmo_cage2d_draw_intern(gz, false, is_highlight, -1);
}

static int gizmo_cage2d_get_cursor(wmGizmo *gz)
{
  int highlight_part = gz->highlight_part;

  if (gz->parent_gzgroup->type->flag & WM_GIZMOGROUPTYPE_3D) {
    return WM_CURSOR_NSEW_SCROLL;
  }

  switch (highlight_part) {
    case ED_GIZMO_CAGE2D_PART_TRANSLATE:
      return WM_CURSOR_NSEW_SCROLL;
    case ED_GIZMO_CAGE2D_PART_SCALE_MIN_X:
    case ED_GIZMO_CAGE2D_PART_SCALE_MAX_X:
      return WM_CURSOR_NSEW_SCROLL;
    case ED_GIZMO_CAGE2D_PART_SCALE_MIN_Y:
    case ED_GIZMO_CAGE2D_PART_SCALE_MAX_Y:
      return WM_CURSOR_NSEW_SCROLL;

      /* TODO: diagonal cursor. */
    case ED_GIZMO_CAGE2D_PART_SCALE_MIN_X_MIN_Y:
    case ED_GIZMO_CAGE2D_PART_SCALE_MAX_X_MIN_Y:
      return WM_CURSOR_NSEW_SCROLL;
    case ED_GIZMO_CAGE2D_PART_SCALE_MIN_X_MAX_Y:
    case ED_GIZMO_CAGE2D_PART_SCALE_MAX_X_MAX_Y:
      return WM_CURSOR_NSEW_SCROLL;
    case ED_GIZMO_CAGE2D_PART_ROTATE:
      return WM_CURSOR_CROSS;
    default:
      return WM_CURSOR_DEFAULT;
  }
}

static int gizmo_cage2d_test_select(bContext *C, wmGizmo *gz, const int mval[2])
{
  float point_local[2];
  float dims[2];
  RNA_float_get_array(gz->ptr, "dimensions", dims);
  const float size_real[2] = {dims[0] / 2.0f, dims[1] / 2.0f};

  if (gizmo_window_project_2d(C, gz, (const float[2]){UNPACK2(mval)}, 2, true, point_local) ==
      false) {
    return -1;
  }

  float margin[2];
  if (!gizmo_calc_rect_view_margin(gz, dims, margin)) {
    return -1;
  }

  /* Expand for hots-pot. */
  const float size[2] = {size_real[0] + margin[0] / 2, size_real[1] + margin[1] / 2};

  const int transform_flag = RNA_enum_get(gz->ptr, "transform");
  const int draw_options = RNA_enum_get(gz->ptr, "draw_options");

  if (transform_flag & ED_GIZMO_CAGE2D_XFORM_FLAG_TRANSLATE) {
    rctf r;
    if (draw_options & ED_GIZMO_CAGE2D_DRAW_FLAG_XFORM_CENTER_HANDLE) {
      r.xmin = -margin[0] / 2;
      r.ymin = -margin[1] / 2;
      r.xmax = margin[0] / 2;
      r.ymax = margin[1] / 2;
    }
    else {
      r.xmin = -size[0] + margin[0];
      r.ymin = -size[1] + margin[1];
      r.xmax = size[0] - margin[0];
      r.ymax = size[1] - margin[1];
    }
    bool isect = BLI_rctf_isect_pt_v(&r, point_local);
    if (isect) {
      return ED_GIZMO_CAGE2D_PART_TRANSLATE;
    }
  }

  /* if gizmo does not have a scale intersection, don't do it */
  if (transform_flag &
      (ED_GIZMO_CAGE2D_XFORM_FLAG_SCALE | ED_GIZMO_CAGE2D_XFORM_FLAG_SCALE_UNIFORM)) {
    const rctf r_xmin = {
        .xmin = -size[0],
        .ymin = -size[1],
        .xmax = -size[0] + margin[0],
        .ymax = size[1],
    };
    const rctf r_xmax = {
        .xmin = size[0] - margin[0],
        .ymin = -size[1],
        .xmax = size[0],
        .ymax = size[1],
    };
    const rctf r_ymin = {
        .xmin = -size[0],
        .ymin = -size[1],
        .xmax = size[0],
        .ymax = -size[1] + margin[1],
    };
    const rctf r_ymax = {
        .xmin = -size[0],
        .ymin = size[1] - margin[1],
        .xmax = size[0],
        .ymax = size[1],
    };

    if (BLI_rctf_isect_pt_v(&r_xmin, point_local)) {
      if (BLI_rctf_isect_pt_v(&r_ymin, point_local)) {
        return ED_GIZMO_CAGE2D_PART_SCALE_MIN_X_MIN_Y;
      }
      if (BLI_rctf_isect_pt_v(&r_ymax, point_local)) {
        return ED_GIZMO_CAGE2D_PART_SCALE_MIN_X_MAX_Y;
      }
      return ED_GIZMO_CAGE2D_PART_SCALE_MIN_X;
    }
    if (BLI_rctf_isect_pt_v(&r_xmax, point_local)) {
      if (BLI_rctf_isect_pt_v(&r_ymin, point_local)) {
        return ED_GIZMO_CAGE2D_PART_SCALE_MAX_X_MIN_Y;
      }
      if (BLI_rctf_isect_pt_v(&r_ymax, point_local)) {
        return ED_GIZMO_CAGE2D_PART_SCALE_MAX_X_MAX_Y;
      }
      return ED_GIZMO_CAGE2D_PART_SCALE_MAX_X;
    }
    if (BLI_rctf_isect_pt_v(&r_ymin, point_local)) {
      return ED_GIZMO_CAGE2D_PART_SCALE_MIN_Y;
    }
    if (BLI_rctf_isect_pt_v(&r_ymax, point_local)) {
      return ED_GIZMO_CAGE2D_PART_SCALE_MAX_Y;
    }
  }

  if (transform_flag & ED_GIZMO_CAGE2D_XFORM_FLAG_ROTATE) {
    /* Rotate:
     *  (*) <-- hot spot is here!
     * +---+
     * |   |
     * +---+ */
    const float r_rotate_pt[2] = {0.0f, size_real[1] + (margin[1] * GIZMO_MARGIN_OFFSET_SCALE)};
    const rctf r_rotate = {
        .xmin = r_rotate_pt[0] - margin[0] / 2.0f,
        .xmax = r_rotate_pt[0] + margin[0] / 2.0f,
        .ymin = r_rotate_pt[1] - margin[1] / 2.0f,
        .ymax = r_rotate_pt[1] + margin[1] / 2.0f,
    };

    if (BLI_rctf_isect_pt_v(&r_rotate, point_local)) {
      return ED_GIZMO_CAGE2D_PART_ROTATE;
    }
  }

  return -1;
}

typedef struct RectTransformInteraction {
  float orig_mouse[2];
  float orig_matrix_offset[4][4];
  float orig_matrix_final_no_offset[4][4];
  Dial *dial;
} RectTransformInteraction;

static void gizmo_cage2d_setup(wmGizmo *gz)
{
  gz->flag |= WM_GIZMO_DRAW_MODAL | WM_GIZMO_DRAW_NO_SCALE;
}

static int gizmo_cage2d_invoke(bContext *C, wmGizmo *gz, const wmEvent *event)
{
  RectTransformInteraction *data = MEM_callocN(sizeof(RectTransformInteraction),
                                               "cage_interaction");

  copy_m4_m4(data->orig_matrix_offset, gz->matrix_offset);
  WM_gizmo_calc_matrix_final_no_offset(gz, data->orig_matrix_final_no_offset);

  if (gizmo_window_project_2d(
          C, gz, (const float[2]){UNPACK2(event->mval)}, 2, false, data->orig_mouse) == 0) {
    zero_v2(data->orig_mouse);
  }

  gz->interaction_data = data;

  return OPERATOR_RUNNING_MODAL;
}

static int gizmo_cage2d_modal(bContext *UNUSED(C),
                              wmGizmo *UNUSED(gz),
                              const wmEvent *UNUSED(event),
                              eWM_GizmoFlagTweak UNUSED(tweak_flag))
{
  return OPERATOR_RUNNING_MODAL;
}

static void gizmo_cage2d_property_update(wmGizmo *gz, wmGizmoProperty *gz_prop)
{
  if (STREQ(gz_prop->type->idname, "matrix")) {
    if (WM_gizmo_target_property_array_length(gz, gz_prop) == 16) {
      WM_gizmo_target_property_float_get_array(gz, gz_prop, &gz->matrix_offset[0][0]);
    }
    else {
      BLI_assert(0);
    }
  }
  else {
    BLI_assert(0);
  }
}

static void gizmo_cage2d_exit(bContext *C, wmGizmo *gz, const bool cancel)
{
  RectTransformInteraction *data = gz->interaction_data;

  MEM_SAFE_FREE(data->dial);

  if (!cancel) {
    return;
  }

  wmGizmoProperty *gz_prop;

  /* reset properties */
  gz_prop = WM_gizmo_target_property_find(gz, "matrix");
  if (gz_prop->type != NULL) {
    WM_gizmo_target_property_float_set_array(C, gz, gz_prop, &data->orig_matrix_offset[0][0]);
  }

  copy_m4_m4(gz->matrix_offset, data->orig_matrix_offset);
}

/* -------------------------------------------------------------------- */
/** \name Cage Gizmo API
 * \{ */

static void GIZMO_GT_cage_2d(wmGizmoType *gzt)
{
  /* identifiers */
  gzt->idname = "GIZMO_GT_cage_2d";

  /* api callbacks */
  gzt->draw = gizmo_cage2d_draw;
  gzt->draw_select = gizmo_cage2d_draw_select;
  gzt->test_select = gizmo_cage2d_test_select;
  gzt->setup = gizmo_cage2d_setup;
  gzt->invoke = gizmo_cage2d_invoke;
  gzt->property_update = gizmo_cage2d_property_update;
  gzt->modal = gizmo_cage2d_modal;
  gzt->exit = gizmo_cage2d_exit;
  gzt->cursor_get = gizmo_cage2d_get_cursor;

  gzt->struct_size = sizeof(wmGizmo);

  /* rna */
  static EnumPropertyItem rna_enum_draw_style[] = {
      {ED_GIZMO_CAGE2D_STYLE_BOX, "BOX", 0, "Box", ""},
      {ED_GIZMO_CAGE2D_STYLE_CIRCLE, "CIRCLE", 0, "Circle", ""},
      {0, NULL, 0, NULL, NULL},
  };
  static EnumPropertyItem rna_enum_transform[] = {
      {ED_GIZMO_CAGE2D_XFORM_FLAG_TRANSLATE, "TRANSLATE", 0, "Move", ""},
      {ED_GIZMO_CAGE2D_XFORM_FLAG_ROTATE, "ROTATE", 0, "Rotate", ""},
      {ED_GIZMO_CAGE2D_XFORM_FLAG_SCALE, "SCALE", 0, "Scale", ""},
      {ED_GIZMO_CAGE2D_XFORM_FLAG_SCALE_UNIFORM, "SCALE_UNIFORM", 0, "Scale Uniform", ""},
      {0, NULL, 0, NULL, NULL},
  };
  static EnumPropertyItem rna_enum_draw_options[] = {
      {ED_GIZMO_CAGE2D_DRAW_FLAG_XFORM_CENTER_HANDLE,
       "XFORM_CENTER_HANDLE",
       0,
       "Center Handle",
       ""},
      {0, NULL, 0, NULL, NULL},
  };
  static float unit_v2[2] = {1.0f, 1.0f};
  RNA_def_float_vector(
      gzt->srna, "dimensions", 2, unit_v2, 0, FLT_MAX, "Dimensions", "", 0.0f, FLT_MAX);
  RNA_def_enum_flag(gzt->srna, "transform", rna_enum_transform, 0, "Transform Options", "");
  RNA_def_enum(gzt->srna,
               "draw_style",
               rna_enum_draw_style,
               ED_GIZMO_CAGE2D_STYLE_CIRCLE,
               "Draw Style",
               "");
  RNA_def_enum_flag(gzt->srna,
                    "draw_options",
                    rna_enum_draw_options,
                    ED_GIZMO_CAGE2D_DRAW_FLAG_XFORM_CENTER_HANDLE,
                    "Draw Options",
                    "");

  WM_gizmotype_target_property_def(gzt, "matrix", PROP_FLOAT, 16);
}

void ED_gizmotypes_cage_2d(void)
{
  WM_gizmotype_append(GIZMO_GT_cage_2d);
}

/** \} */
