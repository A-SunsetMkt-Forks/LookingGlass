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

#include "cursor.h"
#include "common/debug.h"
#include "common/locking.h"
#include "common/option.h"

#include "texture.h"
#include "shader.h"
#include "model.h"
#include "util.h"

#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>

// these headers are auto generated by cmake
#include "cursor.vert.h"
#include "cursor_rgb.frag.h"
#include "cursor_mono.frag.h"

struct CursorTex
{
  struct EGL_Texture * texture;
  struct EGL_Shader  * shader;
  GLuint uMousePos;
  GLuint uScale;
  GLuint uRotate;
  GLuint uCBMode;
};

struct CursorPos
{
  float x, y;
};

struct CursorSize
{
  float w, h;
};

struct EGL_Cursor
{
  LG_Lock           lock;
  LG_RendererCursor type;
  int               width;
  int               height;
  int               stride;
  uint8_t *         data;
  size_t            dataSize;
  bool              update;

  // cursor state
  bool              visible;
  LG_RendererRotate rotate;
  int               cbMode;

  _Atomic(struct CursorPos)  pos;
  _Atomic(struct CursorPos)  hs;
  _Atomic(struct CursorSize) size;
  _Atomic(float)             scale;

  struct CursorTex norm;
  struct CursorTex mono;
  struct EGL_Model * model;
};

static bool cursorTexInit(struct CursorTex * t,
    const char * vertex_code  , size_t vertex_size,
    const char * fragment_code, size_t fragment_size)
{
  if (!egl_textureInit(&t->texture, NULL, EGL_TEXTYPE_BUFFER))
  {
    DEBUG_ERROR("Failed to initialize the cursor texture");
    return false;
  }

  if (!egl_shaderInit(&t->shader))
  {
    DEBUG_ERROR("Failed to initialize the cursor shader");
    return false;
  }

  if (!egl_shaderCompile(t->shader,
        vertex_code, vertex_size, fragment_code, fragment_size, false, NULL))
  {
    DEBUG_ERROR("Failed to compile the cursor shader");
    return false;
  }

  t->uMousePos = egl_shaderGetUniform(t->shader, "mouse"  );
  t->uScale    = egl_shaderGetUniform(t->shader, "scale"  );
  t->uRotate   = egl_shaderGetUniform(t->shader, "rotate" );
  t->uCBMode   = egl_shaderGetUniform(t->shader, "cbMode" );

  return true;
}

static inline void setCursorTexUniforms(EGL_Cursor * cursor,
    struct CursorTex * t, bool mono, float x, float y,
    float w, float h, float scale)
{
  glUniform4f(t->uMousePos, x, y, w, mono ? h / 2 : h);
  glUniform1f(t->uScale   , scale);
  glUniform1i(t->uRotate  , cursor->rotate);
  glUniform1i(t->uCBMode  , cursor->cbMode);
}

static void cursorTexFree(struct CursorTex * t)
{
  egl_textureFree(&t->texture);
  egl_shaderFree (&t->shader );
};

bool egl_cursorInit(EGL_Cursor ** cursor)
{
  *cursor = malloc(sizeof(**cursor));
  if (!*cursor)
  {
    DEBUG_ERROR("Failed to malloc EGL_Cursor");
    return false;
  }

  memset(*cursor, 0, sizeof(**cursor));
  LG_LOCK_INIT((*cursor)->lock);

  if (!cursorTexInit(&(*cursor)->norm,
      b_shader_cursor_vert    , b_shader_cursor_vert_size,
      b_shader_cursor_rgb_frag, b_shader_cursor_rgb_frag_size))
    return false;

  if (!cursorTexInit(&(*cursor)->mono,
      b_shader_cursor_vert     , b_shader_cursor_vert_size,
      b_shader_cursor_mono_frag, b_shader_cursor_mono_frag_size))
    return false;

  if (!egl_modelInit(&(*cursor)->model))
  {
    DEBUG_ERROR("Failed to initialize the cursor model");
    return false;
  }

  egl_modelSetDefault((*cursor)->model, true);

  (*cursor)->cbMode = option_get_int("egl", "cbMode");

  struct CursorPos  pos  = { .x = 0, .y = 0 };
  struct CursorPos  hs   = { .x = 0, .y = 0 };
  struct CursorSize size = { .w = 0, .h = 0 };
  atomic_init(&(*cursor)->pos  , pos );
  atomic_init(&(*cursor)->hs   , hs  );
  atomic_init(&(*cursor)->size , size);
  atomic_init(&(*cursor)->scale, 1.0f);

  return true;
}

void egl_cursorFree(EGL_Cursor ** cursor)
{
  if (!*cursor)
    return;

  LG_LOCK_FREE((*cursor)->lock);
  if ((*cursor)->data)
    free((*cursor)->data);

  cursorTexFree(&(*cursor)->norm);
  cursorTexFree(&(*cursor)->mono);
  egl_modelFree(&(*cursor)->model);

  free(*cursor);
  *cursor = NULL;
}

bool egl_cursorSetShape(EGL_Cursor * cursor, const LG_RendererCursor type,
    const int width, const int height, const int stride, const uint8_t * data)
{
  LG_LOCK(cursor->lock);

  cursor->type   = type;
  cursor->width  = width;
  cursor->height = (type == LG_CURSOR_MONOCHROME ? height / 2 : height);
  cursor->stride = stride;

  const size_t size = height * stride;
  if (size > cursor->dataSize)
  {
    if (cursor->data)
      free(cursor->data);

    cursor->data = malloc(size);
    if (!cursor->data)
    {
      DEBUG_ERROR("Failed to malloc buffer for cursor shape");
      return false;
    }

    cursor->dataSize = size;
  }

  memcpy(cursor->data, data, size);
  cursor->update = true;

  LG_UNLOCK(cursor->lock);
  return true;
}

void egl_cursorSetSize(EGL_Cursor * cursor, const float w, const float h)
{
  struct CursorSize size = { .w = w, .h = h };
  atomic_store(&cursor->size, size);
}

void egl_cursorSetScale(EGL_Cursor * cursor, const float scale)
{
  atomic_store(&cursor->scale, scale);
}

void egl_cursorSetState(EGL_Cursor * cursor, const bool visible,
    const float x, const float y, const float hx, const float hy)
{
  cursor->visible = visible;
  struct CursorPos pos = { .x = x , .y = y  };
  struct CursorPos hs  = { .x = hx, .y = hy };
  atomic_store(&cursor->pos, pos);
  atomic_store(&cursor->hs , hs);
}

struct CursorState egl_cursorRender(EGL_Cursor * cursor,
    LG_RendererRotate rotate, int width, int height)
{
  if (!cursor->visible)
    return (struct CursorState) { .visible = false };

  if (cursor->update)
  {
    LG_LOCK(cursor->lock);
    cursor->update = false;

    uint8_t * data = cursor->data;

    switch(cursor->type)
    {
      case LG_CURSOR_MASKED_COLOR:
      {
        uint32_t xor[cursor->height][cursor->width];
        for(int y = 0; y < cursor->height; ++y)
          for(int x = 0; x < cursor->width; ++x)
          {
            uint32_t * src = (uint32_t *)(data + (cursor->stride * y) + x * 4);
            const bool masked = (*src & 0xFF000000) != 0;
            if (masked)
              *src = xor[y][x] = *src & 0x00FFFFFF;
            else
            {
              xor[y][x]  = 0xFF000000;
              *src      |= 0xFF000000;
            }
          }

        egl_textureSetup(cursor->mono.texture, EGL_PF_BGRA,
            cursor->width, cursor->height, cursor->width, sizeof(xor[0]));
        egl_textureUpdate(cursor->mono.texture, (uint8_t *)xor, true);
      }
      // fall through

      case LG_CURSOR_COLOR:
      {
        egl_textureSetup(cursor->norm.texture, EGL_PF_BGRA,
            cursor->width, cursor->height, cursor->width, cursor->stride);
        egl_textureUpdate(cursor->norm.texture, data, true);
        break;
      }

      case LG_CURSOR_MONOCHROME:
      {
        uint32_t and[cursor->height][cursor->width];
        uint32_t xor[cursor->height][cursor->width];

        for(int y = 0; y < cursor->height; ++y)
        {
          for(int x = 0; x < cursor->width; ++x)
          {
            const uint8_t  * srcAnd  = data + (cursor->stride * y) + (x / 8);
            const uint8_t  * srcXor  = srcAnd + cursor->stride * cursor->height;
            const uint8_t    mask    = 0x80 >> (x % 8);
            const uint32_t   andMask = (*srcAnd & mask) ? 0xFFFFFFFF : 0xFF000000;
            const uint32_t   xorMask = (*srcXor & mask) ? 0x00FFFFFF : 0x00000000;

            and[y][x] = andMask;
            xor[y][x] = xorMask;
          }
        }

        egl_textureSetup(cursor->norm.texture, EGL_PF_BGRA,
            cursor->width, cursor->height, cursor->width, sizeof(and[0]));
        egl_textureSetup(cursor->mono.texture, EGL_PF_BGRA,
            cursor->width, cursor->height, cursor->width, sizeof(xor[0]));
        egl_textureUpdate(cursor->norm.texture, (uint8_t *)and, true);
        egl_textureUpdate(cursor->mono.texture, (uint8_t *)xor, true);
        break;
      }
    }
    LG_UNLOCK(cursor->lock);
  }

  cursor->rotate = rotate;

  struct CursorPos  pos   = atomic_load(&cursor->pos  );
  float             scale = atomic_load(&cursor->scale);
  struct CursorPos  hs    = atomic_load(&cursor->hs   );
  struct CursorSize size  = atomic_load(&cursor->size );

  pos.x  -= hs.x * scale;
  pos.y  -= hs.y * scale;
  size.w *= scale;
  size.h *= scale;

  struct CursorState state = {
    .visible = true,
  };

  switch (rotate)
  {
    case LG_ROTATE_0:
      state.rect.x = (pos.x * width + width) / 2;
      state.rect.y = (-pos.y * height + height) / 2 - size.h * height;
      state.rect.w = size.w * width + 3;
      state.rect.h = size.h * height + 3;
      break;

    case LG_ROTATE_90:
      state.rect.x = (-pos.y * width + width) / 2 - size.h * width;
      state.rect.y = (-pos.x * height + height) / 2 - size.w * height;
      state.rect.w = size.h * width + 3;
      state.rect.h = size.w * height + 3;
      break;

    case LG_ROTATE_180:
      state.rect.x = (-pos.x * width + width) / 2 - size.w * width;
      state.rect.y = (pos.y * height + height) / 2;
      state.rect.w = size.w * width + 3;
      state.rect.h = size.h * height + 3;
      break;

    case LG_ROTATE_270:
      state.rect.x = (pos.y * width + width) / 2;
      state.rect.y = (pos.x * height + height) / 2;
      state.rect.w = size.h * width + 3;
      state.rect.h = size.w * height + 3;
      break;

    default:
      DEBUG_UNREACHABLE();
  }

  state.rect.x = max(0, state.rect.x - 1);
  state.rect.y = max(0, state.rect.y - 1);

  glEnable(GL_BLEND);
  switch(cursor->type)
  {
    case LG_CURSOR_MONOCHROME:
    {
      egl_shaderUse(cursor->norm.shader);
      setCursorTexUniforms(cursor, &cursor->norm, true, pos.x, pos.y,
          size.w, size.h, scale);
      glBlendFunc(GL_ZERO, GL_SRC_COLOR);
      egl_modelSetTexture(cursor->model, cursor->norm.texture);
      egl_modelRender(cursor->model);

      egl_shaderUse(cursor->mono.shader);
      setCursorTexUniforms(cursor, &cursor->mono, true, pos.x, pos.y,
          size.w, size.h, scale);
      glBlendFunc(GL_ONE_MINUS_DST_COLOR, GL_ZERO);
      egl_modelSetTexture(cursor->model, cursor->mono.texture);
      egl_modelRender(cursor->model);
      break;
    }

    case LG_CURSOR_MASKED_COLOR:
    {
      egl_shaderUse(cursor->norm.shader);
      setCursorTexUniforms(cursor, &cursor->norm, false, pos.x, pos.y,
          size.w, size.h, scale);
      glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
      egl_modelSetTexture(cursor->model, cursor->norm.texture);
      egl_modelRender(cursor->model);

      egl_shaderUse(cursor->mono.shader);
      setCursorTexUniforms(cursor, &cursor->mono, false, pos.x, pos.y,
          size.w, size.h, scale);
      glBlendFunc(GL_ONE_MINUS_DST_COLOR, GL_ZERO);
      egl_modelSetTexture(cursor->model, cursor->mono.texture);
      egl_modelRender(cursor->model);
      break;
    }

    case LG_CURSOR_COLOR:
    {
      egl_shaderUse(cursor->norm.shader);
      setCursorTexUniforms(cursor, &cursor->norm, false, pos.x, pos.y,
          size.w, size.h, scale);
      glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
      egl_modelSetTexture(cursor->model, cursor->norm.texture);
      egl_modelRender(cursor->model);
      break;
    }
  }
  glDisable(GL_BLEND);

  return state;
}
