#include "include/synthetic_texture/yuv_pattern_texture.h"

#include <epoxy/gl.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

struct SmpteBar {
  uint8_t r;
  uint8_t g;
  uint8_t b;
};

static constexpr SmpteBar kSmpteBars[] = {
    {235, 235, 235}, {235, 235, 16}, {16, 235, 235}, {16, 235, 16},
    {235, 16, 235},  {235, 16, 16},  {16, 16, 235},
};
static constexpr int kSmpteBarCount = 7;

static void rgb_to_yuv709(uint8_t r,
                          uint8_t g,
                          uint8_t b,
                          uint8_t* y,
                          uint8_t* u,
                          uint8_t* v) {
  const float rf = r / 255.f;
  const float gf = g / 255.f;
  const float bf = b / 255.f;
  const float yf = 0.2126f * rf + 0.7152f * gf + 0.0722f * bf;
  const float uf = (bf - yf) / 1.8556f + 0.5f;
  const float vf = (rf - yf) / 1.5748f + 0.5f;
  *y = static_cast<uint8_t>(std::clamp(yf * 255.f, 0.f, 255.f));
  *u = static_cast<uint8_t>(std::clamp(uf * 255.f, 0.f, 255.f));
  *v = static_cast<uint8_t>(std::clamp(vf * 255.f, 0.f, 255.f));
}

static void fill_planar_yuv420_smpte(uint8_t* y_plane,
                                     uint8_t* u_plane,
                                     uint8_t* v_plane,
                                     int width,
                                     int height,
                                     int64_t frame) {
  const int chroma_width = width / 2;
  const int chroma_height = height / 2;
  const int stripe_x = static_cast<int>((frame * 6) % width);

  std::vector<uint8_t> full_u(static_cast<size_t>(width * height));
  std::vector<uint8_t> full_v(static_cast<size_t>(width * height));

  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
      const int bar = (x * kSmpteBarCount) / width;
      uint8_t r = kSmpteBars[bar].r;
      uint8_t g = kSmpteBars[bar].g;
      uint8_t b = kSmpteBars[bar].b;
      if (x >= stripe_x && x < stripe_x + 12) {
        r = static_cast<uint8_t>(255 - r);
        g = static_cast<uint8_t>(255 - g);
        b = static_cast<uint8_t>(255 - b);
      }

      uint8_t y_val = 0;
      uint8_t u_val = 0;
      uint8_t v_val = 0;
      rgb_to_yuv709(r, g, b, &y_val, &u_val, &v_val);
      const size_t index = static_cast<size_t>(y * width + x);
      y_plane[index] = y_val;
      full_u[index] = u_val;
      full_v[index] = v_val;
    }
  }

  for (int cy = 0; cy < chroma_height; ++cy) {
    for (int cx = 0; cx < chroma_width; ++cx) {
      int sum_u = 0;
      int sum_v = 0;
      for (int dy = 0; dy < 2; ++dy) {
        for (int dx = 0; dx < 2; ++dx) {
          const size_t index = static_cast<size_t>((cy * 2 + dy) * width + (cx * 2 + dx));
          sum_u += full_u[index];
          sum_v += full_v[index];
        }
      }
      const size_t c_index = static_cast<size_t>(cy * chroma_width + cx);
      u_plane[c_index] = static_cast<uint8_t>(sum_u / 4);
      v_plane[c_index] = static_cast<uint8_t>(sum_v / 4);
    }
  }
}

static const char* kVertexShader = R"(attribute vec2 a_position;
varying vec2 v_tex_coord;
void main() {
  v_tex_coord = a_position * 0.5 + 0.5;
  gl_Position = vec4(a_position, 0.0, 1.0);
})";

static const char* kFragmentShader = R"(precision mediump float;
varying vec2 v_tex_coord;
uniform sampler2D u_y;
uniform sampler2D u_u;
uniform sampler2D u_v;
void main() {
  float y = texture2D(u_y, v_tex_coord).r;
  float u = texture2D(u_u, v_tex_coord).r - 0.5;
  float v = texture2D(u_v, v_tex_coord).r - 0.5;
  float r = y + 1.5748 * v;
  float g = y - 0.1873 * u - 0.4681 * v;
  float b = y + 1.8556 * u;
  gl_FragColor = vec4(clamp(r, 0.0, 1.0), clamp(g, 0.0, 1.0), clamp(b, 0.0, 1.0), 1.0);
})";

static GLuint compile_shader(GLenum type, const char* source) {
  const GLuint shader = glCreateShader(type);
  glShaderSource(shader, 1, &source, nullptr);
  glCompileShader(shader);
  GLint compiled = GL_FALSE;
  glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
  if (!compiled) {
    glDeleteShader(shader);
    return 0;
  }
  return shader;
}

static GLuint link_program(GLuint vertex_shader, GLuint fragment_shader) {
  const GLuint program = glCreateProgram();
  glAttachShader(program, vertex_shader);
  glAttachShader(program, fragment_shader);
  glBindAttribLocation(program, 0, "a_position");
  glLinkProgram(program);
  GLint linked = GL_FALSE;
  glGetProgramiv(program, GL_LINK_STATUS, &linked);
  if (!linked) {
    glDeleteProgram(program);
    return 0;
  }
  return program;
}

struct _SyntheticYuvPatternTexture {
  FlTextureGL parent_instance;
  guint32 output_texture;
  guint32 fbo;
  guint32 y_texture;
  guint32 u_texture;
  guint32 v_texture;
  GLuint program;
  GLuint vbo;
  gboolean gl_ready;
  gint width;
  gint height;
  gint64 frame;
  std::vector<uint8_t> y_plane;
  std::vector<uint8_t> u_plane;
  std::vector<uint8_t> v_plane;
};

G_DEFINE_TYPE(SyntheticYuvPatternTexture,
              synthetic_yuv_pattern_texture,
              fl_texture_gl_get_type())

static void synthetic_yuv_pattern_texture_init(SyntheticYuvPatternTexture* self) {
  self->output_texture = 0;
  self->fbo = 0;
  self->y_texture = 0;
  self->u_texture = 0;
  self->v_texture = 0;
  self->program = 0;
  self->vbo = 0;
  self->gl_ready = FALSE;
  self->width = 0;
  self->height = 0;
  self->frame = 0;
}

static void delete_yuv_gl_resources(SyntheticYuvPatternTexture* self) {
  if (gdk_gl_context_get_current() == nullptr) {
    return;
  }
  if (self->program != 0) {
    glDeleteProgram(self->program);
    self->program = 0;
  }
  if (self->vbo != 0) {
    glDeleteBuffers(1, &self->vbo);
    self->vbo = 0;
  }
  if (self->y_texture != 0) {
    glDeleteTextures(1, &self->y_texture);
    self->y_texture = 0;
  }
  if (self->u_texture != 0) {
    glDeleteTextures(1, &self->u_texture);
    self->u_texture = 0;
  }
  if (self->v_texture != 0) {
    glDeleteTextures(1, &self->v_texture);
    self->v_texture = 0;
  }
  if (self->output_texture != 0) {
    glDeleteTextures(1, &self->output_texture);
    self->output_texture = 0;
  }
  if (self->fbo != 0) {
    glDeleteFramebuffers(1, &self->fbo);
    self->fbo = 0;
  }
  self->gl_ready = FALSE;
}

static void synthetic_yuv_pattern_texture_dispose(GObject* object) {
  delete_yuv_gl_resources(SYNTHETIC_YUV_PATTERN_TEXTURE(object));
  G_OBJECT_CLASS(synthetic_yuv_pattern_texture_parent_class)->dispose(object);
}

static void upload_luminance_plane(guint32 texture,
                                   int width,
                                   int height,
                                   const uint8_t* data) {
  glBindTexture(GL_TEXTURE_2D, texture);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE, width, height, 0, GL_LUMINANCE,
               GL_UNSIGNED_BYTE, data);
  glBindTexture(GL_TEXTURE_2D, 0);
}

static void upload_luminance_subimage(guint32 texture,
                                      int width,
                                      int height,
                                      const uint8_t* data) {
  glBindTexture(GL_TEXTURE_2D, texture);
  glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, width, height, GL_LUMINANCE,
                  GL_UNSIGNED_BYTE, data);
  glBindTexture(GL_TEXTURE_2D, 0);
}

static gboolean ensure_yuv_gl(SyntheticYuvPatternTexture* self, GError** error) {
  if (self->gl_ready) {
    return TRUE;
  }

  const int width = self->width;
  const int height = self->height;
  const int chroma_width = width / 2;
  const int chroma_height = height / 2;

  const GLuint vertex_shader = compile_shader(GL_VERTEX_SHADER, kVertexShader);
  const GLuint fragment_shader =
      compile_shader(GL_FRAGMENT_SHADER, kFragmentShader);
  if (vertex_shader == 0 || fragment_shader == 0) {
    g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED,
                "Failed to compile YUV shader");
    return FALSE;
  }

  self->program = link_program(vertex_shader, fragment_shader);
  glDeleteShader(vertex_shader);
  glDeleteShader(fragment_shader);
  if (self->program == 0) {
    g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED,
                "Failed to link YUV shader program");
    return FALSE;
  }

  static const float kQuad[] = {-1.f, -1.f, 1.f, -1.f, -1.f, 1.f, 1.f, 1.f};
  glGenBuffers(1, &self->vbo);
  glBindBuffer(GL_ARRAY_BUFFER, self->vbo);
  glBufferData(GL_ARRAY_BUFFER, sizeof(kQuad), kQuad, GL_STATIC_DRAW);
  glBindBuffer(GL_ARRAY_BUFFER, 0);

  glGenTextures(1, &self->y_texture);
  glGenTextures(1, &self->u_texture);
  glGenTextures(1, &self->v_texture);
  upload_luminance_plane(self->y_texture, width, height, self->y_plane.data());
  upload_luminance_plane(self->u_texture, chroma_width, chroma_height,
                         self->u_plane.data());
  upload_luminance_plane(self->v_texture, chroma_width, chroma_height,
                         self->v_plane.data());

  GLint previous_fbo = 0;
  glGetIntegerv(GL_FRAMEBUFFER_BINDING, &previous_fbo);
  glGenFramebuffers(1, &self->fbo);
  glBindFramebuffer(GL_FRAMEBUFFER, self->fbo);
  glGenTextures(1, &self->output_texture);
  glBindTexture(GL_TEXTURE_2D, self->output_texture);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA,
               GL_UNSIGNED_BYTE, nullptr);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                           self->output_texture, 0);

  if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
    glBindFramebuffer(GL_FRAMEBUFFER, previous_fbo);
    g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED,
                "Failed to create YUV output FBO");
    return FALSE;
  }

  glBindFramebuffer(GL_FRAMEBUFFER, previous_fbo);
  glBindTexture(GL_TEXTURE_2D, 0);
  self->gl_ready = TRUE;
  return TRUE;
}

static void render_yuv_to_output(SyntheticYuvPatternTexture* self) {
  const int width = self->width;
  const int height = self->height;
  const int chroma_width = width / 2;
  const int chroma_height = height / 2;

  upload_luminance_subimage(self->y_texture, width, height, self->y_plane.data());
  upload_luminance_subimage(self->u_texture, chroma_width, chroma_height,
                            self->u_plane.data());
  upload_luminance_subimage(self->v_texture, chroma_width, chroma_height,
                            self->v_plane.data());

  GLint previous_fbo = 0;
  glGetIntegerv(GL_FRAMEBUFFER_BINDING, &previous_fbo);

  glBindFramebuffer(GL_FRAMEBUFFER, self->fbo);
  glViewport(0, 0, width, height);
  glUseProgram(self->program);

  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, self->y_texture);
  glUniform1i(glGetUniformLocation(self->program, "u_y"), 0);

  glActiveTexture(GL_TEXTURE1);
  glBindTexture(GL_TEXTURE_2D, self->u_texture);
  glUniform1i(glGetUniformLocation(self->program, "u_u"), 1);

  glActiveTexture(GL_TEXTURE2);
  glBindTexture(GL_TEXTURE_2D, self->v_texture);
  glUniform1i(glGetUniformLocation(self->program, "u_v"), 2);

  glBindBuffer(GL_ARRAY_BUFFER, self->vbo);
  glEnableVertexAttribArray(0);
  glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, nullptr);
  glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
  glDisableVertexAttribArray(0);
  glBindBuffer(GL_ARRAY_BUFFER, 0);

  glBindTexture(GL_TEXTURE_2D, 0);
  glUseProgram(0);
  glFlush();
  glBindFramebuffer(GL_FRAMEBUFFER, previous_fbo);
}

static gboolean synthetic_yuv_pattern_texture_populate(
    FlTextureGL* texture,
    guint32* target,
    guint32* name,
    guint32* width,
    guint32* height,
    GError** error) {
  SyntheticYuvPatternTexture* self = SYNTHETIC_YUV_PATTERN_TEXTURE(texture);

  self->frame++;
  const size_t luma_bytes =
      static_cast<size_t>(self->width) * static_cast<size_t>(self->height);
  const size_t chroma_bytes = luma_bytes / 4;
  if (self->y_plane.size() != luma_bytes) {
    self->y_plane.resize(luma_bytes);
    self->u_plane.resize(chroma_bytes);
    self->v_plane.resize(chroma_bytes);
  }

  fill_planar_yuv420_smpte(self->y_plane.data(), self->u_plane.data(),
                           self->v_plane.data(), self->width, self->height,
                           self->frame);

  if (!ensure_yuv_gl(self, error)) {
    return FALSE;
  }

  render_yuv_to_output(self);

  *target = GL_TEXTURE_2D;
  *name = self->output_texture;
  *width = static_cast<guint32>(self->width);
  *height = static_cast<guint32>(self->height);
  return TRUE;
}

static void synthetic_yuv_pattern_texture_class_init(
    SyntheticYuvPatternTextureClass* klass) {
  FL_TEXTURE_GL_CLASS(klass)->populate = synthetic_yuv_pattern_texture_populate;
  G_OBJECT_CLASS(klass)->dispose = synthetic_yuv_pattern_texture_dispose;
}

SyntheticYuvPatternTexture* synthetic_yuv_pattern_texture_new(gint width,
                                                              gint height) {
  SyntheticYuvPatternTexture* self = SYNTHETIC_YUV_PATTERN_TEXTURE(
      g_object_new(synthetic_yuv_pattern_texture_get_type(), nullptr));
  self->width = width;
  self->height = height;
  return self;
}
