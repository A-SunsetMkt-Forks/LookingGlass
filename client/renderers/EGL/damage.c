/**
 * Looking Glass
 * Copyright © 2017-2025 The Looking Glass Authors
 * https://looking-glass.io
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc., 59
 * Temple Place, Suite 330, Boston, MA 02111-1307 USA
 */

#include "damage.h"
#include "common/debug.h"
#include "common/KVMFR.h"
#include "common/locking.h"

#include "app.h"
#include "desktop_rects.h"
#include "shader.h"
#include "cimgui.h"

#include <stdlib.h>
#include <string.h>

// these headers are auto generated by cmake
#include "damage.vert.h"
#include "damage.frag.h"

struct EGL_Damage
{
  EGL_Shader       * shader;
  EGL_DesktopRects * mesh;
  GLfloat            transform[6];

  bool          show;

  int   width     , height;
  float translateX, translateY;
  float scaleX    , scaleY;
  LG_RendererRotate rotate;

  // uniforms
  GLint uTransform;
};

void egl_damageConfigUI(EGL_Damage * damage)
{
  igCheckbox("Show damage overlay", &damage->show);
}

bool egl_damageInit(EGL_Damage ** damage)
{
  *damage = malloc(sizeof(**damage));
  if (!*damage)
  {
    DEBUG_ERROR("Failed to malloc EGL_Damage");
    return false;
  }

  memset(*damage, 0, sizeof(EGL_Damage));

  if (!egl_shaderInit(&(*damage)->shader))
  {
    DEBUG_ERROR("Failed to initialize the damage shader");
    return false;
  }

  if (!egl_shaderCompile((*damage)->shader,
        b_shader_damage_vert, b_shader_damage_vert_size,
        b_shader_damage_frag, b_shader_damage_frag_size,
        false, NULL))
  {
    DEBUG_ERROR("Failed to compile the damage shader");
    return false;
  }

  if (!egl_desktopRectsInit(&(*damage)->mesh, KVMFR_MAX_DAMAGE_RECTS))
  {
    DEBUG_ERROR("Failed to initialize the mesh");
    return false;
  }

  (*damage)->uTransform = egl_shaderGetUniform((*damage)->shader, "transform");

  return true;
}

void egl_damageFree(EGL_Damage ** damage)
{
  if (!*damage)
    return;

  egl_desktopRectsFree(&(*damage)->mesh);
  egl_shaderFree(&(*damage)->shader);

  free(*damage);
  *damage = NULL;
}

static void update_matrix(EGL_Damage * damage)
{
  egl_desktopRectsMatrix(damage->transform, damage->width, damage->height,
    damage->translateX, damage->translateY, damage->scaleX, damage->scaleY, damage->rotate);
}

void egl_damageSetup(EGL_Damage * damage, int width, int height)
{
  damage->width  = width;
  damage->height = height;
  update_matrix(damage);
}

void egl_damageResize(EGL_Damage * damage, float translateX, float translateY,
    float scaleX, float scaleY)
{
  damage->translateX = translateX;
  damage->translateY = translateY;
  damage->scaleX     = scaleX;
  damage->scaleY     = scaleY;
  update_matrix(damage);
}

bool egl_damageRender(EGL_Damage * damage, LG_RendererRotate rotate, const struct DesktopDamage * data)
{
  if (!damage->show)
    return false;

  if (rotate != damage->rotate)
  {
    damage->rotate = rotate;
    update_matrix(damage);
  }

  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

  egl_shaderUse(damage->shader);
  glUniformMatrix3x2fv(damage->uTransform, 1, GL_FALSE, damage->transform);

  if (data && data->count != 0)
    egl_desktopRectsUpdate(damage->mesh, (const struct DamageRects *) data,
        damage->width, damage->height);

  egl_desktopRectsRender(damage->mesh);

  glDisable(GL_BLEND);
  return true;
}
