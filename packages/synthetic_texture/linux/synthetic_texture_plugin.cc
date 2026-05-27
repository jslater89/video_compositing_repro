#include "include/synthetic_texture/synthetic_texture_plugin.h"
#include "include/synthetic_texture/yuv_pattern_texture.h"

#include <epoxy/gl.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <vector>

struct RgbaSequenceHeader {
  char magic[4];
  uint32_t width;
  uint32_t height;
  uint32_t frame_count;
};

static bool read_rgba_sequence_file(const char* path,
                                    RgbaSequenceHeader* header,
                                    std::vector<uint8_t>* pixels) {
  std::ifstream input(path, std::ios::binary | std::ios::ate);
  if (!input) {
    return false;
  }

  const std::streamsize file_size = input.tellg();
  if (file_size < static_cast<std::streamsize>(sizeof(RgbaSequenceHeader))) {
    return false;
  }

  input.seekg(0, std::ios::beg);
  input.read(reinterpret_cast<char*>(header), sizeof(RgbaSequenceHeader));
  if (!input ||
      std::memcmp(header->magic, "RGBQ", 4) != 0 ||
      header->width == 0 || header->height == 0 || header->frame_count == 0) {
    return false;
  }

  const size_t frame_bytes =
      static_cast<size_t>(header->width) * static_cast<size_t>(header->height) * 4;
  const size_t payload_bytes = frame_bytes * header->frame_count;
  if (static_cast<size_t>(file_size) !=
      sizeof(RgbaSequenceHeader) + payload_bytes) {
    return false;
  }

  pixels->resize(payload_bytes);
  input.read(reinterpret_cast<char*>(pixels->data()),
             static_cast<std::streamsize>(payload_bytes));
  return static_cast<bool>(input);
}

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

static void fill_smpte_bars_rgba(uint8_t* pixels,
                                 int width,
                                 int height,
                                 int64_t frame) {
  const int stripe_x = static_cast<int>((frame * 6) % width);

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

      const size_t index = static_cast<size_t>((y * width + x) * 4);
      pixels[index] = r;
      pixels[index + 1] = g;
      pixels[index + 2] = b;
      pixels[index + 3] = 255;
    }
  }
}

static bool ensure_fbo_rgba_texture(guint32* fbo,
                                    guint32* texture,
                                    gint width,
                                    gint height) {
  if (*fbo != 0) {
    return true;
  }

  GLint previous_fbo = 0;
  glGetIntegerv(GL_FRAMEBUFFER_BINDING, &previous_fbo);

  glGenFramebuffers(1, fbo);
  glBindFramebuffer(GL_FRAMEBUFFER, *fbo);
  glGenTextures(1, texture);
  glBindTexture(GL_TEXTURE_2D, *texture);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA,
               GL_UNSIGNED_BYTE, nullptr);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                         *texture, 0);

  const bool complete =
      glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;

  glBindFramebuffer(GL_FRAMEBUFFER, previous_fbo);
  glBindTexture(GL_TEXTURE_2D, 0);
  return complete;
}

static void upload_rgba_to_fbo_texture(guint32 fbo,
                                       guint32 texture,
                                       gint width,
                                       gint height,
                                       const uint8_t* pixels) {
  GLint previous_fbo = 0;
  glGetIntegerv(GL_FRAMEBUFFER_BINDING, &previous_fbo);

  // FVP exposes the FBO color attachment as the Flutter texture. Update that
  // attachment in place (GLES has no glDrawPixels).
  glBindTexture(GL_TEXTURE_2D, texture);
  glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, width, height, GL_RGBA,
                  GL_UNSIGNED_BYTE, pixels);
  glBindTexture(GL_TEXTURE_2D, 0);

  glBindFramebuffer(GL_FRAMEBUFFER, fbo);
  glFlush();
  glBindFramebuffer(GL_FRAMEBUFFER, previous_fbo);
}

static void delete_gl_texture_and_fbo(guint32* texture, guint32* fbo) {
  if (gdk_gl_context_get_current() == nullptr) {
    return;
  }
  if (*texture != 0) {
    glDeleteTextures(1, texture);
    *texture = 0;
  }
  if (*fbo != 0) {
    glDeleteFramebuffers(1, fbo);
    *fbo = 0;
  }
}

static bool read_use_fbo_arg(FlValue* args) {
  FlValue* use_fbo_value = fl_value_lookup_string(args, "useFbo");
  return use_fbo_value != nullptr &&
         fl_value_get_type(use_fbo_value) == FL_VALUE_TYPE_BOOL &&
         fl_value_get_bool(use_fbo_value);
}

G_DECLARE_FINAL_TYPE(SyntheticPatternTexture,
                     synthetic_pattern_texture,
                     SYNTHETIC,
                     PATTERN_TEXTURE,
                     FlTextureGL)

#define SYNTHETIC_PATTERN_TEXTURE(obj)                                       \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), synthetic_pattern_texture_get_type(), \
                              SyntheticPatternTexture))

struct _SyntheticPatternTexture {
  FlTextureGL parent_instance;
  FlTextureRegistrar* registrar;
  guint32 gl_texture;
  guint32 fbo;
  gboolean use_fbo;
  gint width;
  gint height;
  gint64 frame;
  std::vector<uint8_t> pixels;
};

G_DEFINE_TYPE(SyntheticPatternTexture,
              synthetic_pattern_texture,
              fl_texture_gl_get_type())

static void synthetic_pattern_texture_init(SyntheticPatternTexture* self) {
  self->registrar = nullptr;
  self->gl_texture = 0;
  self->fbo = 0;
  self->use_fbo = FALSE;
  self->width = 0;
  self->height = 0;
  self->frame = 0;
}

static void synthetic_pattern_texture_dispose(GObject* object) {
  SyntheticPatternTexture* self = SYNTHETIC_PATTERN_TEXTURE(object);
  delete_gl_texture_and_fbo(&self->gl_texture, &self->fbo);
  G_OBJECT_CLASS(synthetic_pattern_texture_parent_class)->dispose(object);
}

static gboolean synthetic_pattern_texture_populate(FlTextureGL* texture,
                                                   guint32* target,
                                                   guint32* name,
                                                   guint32* width,
                                                   guint32* height,
                                                   GError** error) {
  SyntheticPatternTexture* self = SYNTHETIC_PATTERN_TEXTURE(texture);

  if (self->use_fbo) {
    if (!ensure_fbo_rgba_texture(&self->fbo, &self->gl_texture, self->width,
                                 self->height)) {
      g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED,
                  "Failed to create FBO for synthetic pattern texture");
      return FALSE;
    }
  } else if (self->gl_texture == 0) {
    glGenTextures(1, &self->gl_texture);
    glBindTexture(GL_TEXTURE_2D, self->gl_texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, self->width, self->height, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glBindTexture(GL_TEXTURE_2D, 0);
  }

  self->frame++;
  const size_t byte_count =
      static_cast<size_t>(self->width) * static_cast<size_t>(self->height) * 4;
  if (self->pixels.size() != byte_count) {
    self->pixels.resize(byte_count);
  }
  fill_smpte_bars_rgba(self->pixels.data(), self->width, self->height,
                       self->frame);

  if (self->use_fbo) {
    upload_rgba_to_fbo_texture(self->fbo, self->gl_texture, self->width,
                               self->height, self->pixels.data());
  } else {
    glBindTexture(GL_TEXTURE_2D, self->gl_texture);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, self->width, self->height, GL_RGBA,
                    GL_UNSIGNED_BYTE, self->pixels.data());
    glBindTexture(GL_TEXTURE_2D, 0);
  }

  *target = GL_TEXTURE_2D;
  *name = self->gl_texture;
  *width = static_cast<guint32>(self->width);
  *height = static_cast<guint32>(self->height);
  return TRUE;
}

static void synthetic_pattern_texture_class_init(
    SyntheticPatternTextureClass* klass) {
  FL_TEXTURE_GL_CLASS(klass)->populate = synthetic_pattern_texture_populate;
  G_OBJECT_CLASS(klass)->dispose = synthetic_pattern_texture_dispose;
}

static SyntheticPatternTexture* synthetic_pattern_texture_new(
    FlTextureRegistrar* registrar,
    gint width,
    gint height,
    gboolean use_fbo) {
  SyntheticPatternTexture* self = SYNTHETIC_PATTERN_TEXTURE(
      g_object_new(synthetic_pattern_texture_get_type(), nullptr));
  self->registrar = registrar;
  self->width = width;
  self->height = height;
  self->use_fbo = use_fbo;
  return self;
}

G_DECLARE_FINAL_TYPE(SyntheticRgbaSequenceTexture,
                     synthetic_rgba_sequence_texture,
                     SYNTHETIC,
                     RGBA_SEQUENCE_TEXTURE,
                     FlTextureGL)

#define SYNTHETIC_RGBA_SEQUENCE_TEXTURE(obj)                               \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),                                         \
                              synthetic_rgba_sequence_texture_get_type(),    \
                              SyntheticRgbaSequenceTexture))

struct _SyntheticRgbaSequenceTexture {
  FlTextureGL parent_instance;
  guint32 gl_texture;
  guint32 fbo;
  gboolean use_fbo;
  gint width;
  gint height;
  gint frame_count;
  gint64 frame_index;
  std::vector<uint8_t> pixels;
};

G_DEFINE_TYPE(SyntheticRgbaSequenceTexture,
              synthetic_rgba_sequence_texture,
              fl_texture_gl_get_type())

static void synthetic_rgba_sequence_texture_init(
    SyntheticRgbaSequenceTexture* self) {
  self->gl_texture = 0;
  self->fbo = 0;
  self->use_fbo = FALSE;
  self->width = 0;
  self->height = 0;
  self->frame_count = 0;
  self->frame_index = 0;
}

static void synthetic_rgba_sequence_texture_dispose(GObject* object) {
  SyntheticRgbaSequenceTexture* self = SYNTHETIC_RGBA_SEQUENCE_TEXTURE(object);
  delete_gl_texture_and_fbo(&self->gl_texture, &self->fbo);
  G_OBJECT_CLASS(synthetic_rgba_sequence_texture_parent_class)->dispose(object);
}

static gboolean synthetic_rgba_sequence_texture_populate(
    FlTextureGL* texture,
    guint32* target,
    guint32* name,
    guint32* width,
    guint32* height,
    GError** error) {
  SyntheticRgbaSequenceTexture* self = SYNTHETIC_RGBA_SEQUENCE_TEXTURE(texture);

  if (self->use_fbo) {
    if (!ensure_fbo_rgba_texture(&self->fbo, &self->gl_texture, self->width,
                                 self->height)) {
      g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED,
                  "Failed to create FBO for RGBA sequence texture");
      return FALSE;
    }
  } else if (self->gl_texture == 0) {
    glGenTextures(1, &self->gl_texture);
    glBindTexture(GL_TEXTURE_2D, self->gl_texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, self->width, self->height, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glBindTexture(GL_TEXTURE_2D, 0);
  }

  const size_t frame_bytes =
      static_cast<size_t>(self->width) * static_cast<size_t>(self->height) * 4;
  const size_t frame_offset =
      frame_bytes * static_cast<size_t>(self->frame_index % self->frame_count);

  if (self->use_fbo) {
    upload_rgba_to_fbo_texture(self->fbo, self->gl_texture, self->width,
                               self->height, self->pixels.data() + frame_offset);
  } else {
    glBindTexture(GL_TEXTURE_2D, self->gl_texture);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, self->width, self->height, GL_RGBA,
                    GL_UNSIGNED_BYTE, self->pixels.data() + frame_offset);
    glBindTexture(GL_TEXTURE_2D, 0);
  }

  self->frame_index++;

  *target = GL_TEXTURE_2D;
  *name = self->gl_texture;
  *width = static_cast<guint32>(self->width);
  *height = static_cast<guint32>(self->height);
  return TRUE;
}

static void synthetic_rgba_sequence_texture_class_init(
    SyntheticRgbaSequenceTextureClass* klass) {
  FL_TEXTURE_GL_CLASS(klass)->populate =
      synthetic_rgba_sequence_texture_populate;
  G_OBJECT_CLASS(klass)->dispose = synthetic_rgba_sequence_texture_dispose;
}

static SyntheticRgbaSequenceTexture* synthetic_rgba_sequence_texture_new(
    const RgbaSequenceHeader* header,
    std::vector<uint8_t> pixels,
    gboolean use_fbo) {
  SyntheticRgbaSequenceTexture* self = SYNTHETIC_RGBA_SEQUENCE_TEXTURE(
      g_object_new(synthetic_rgba_sequence_texture_get_type(), nullptr));
  self->width = static_cast<gint>(header->width);
  self->height = static_cast<gint>(header->height);
  self->frame_count = static_cast<gint>(header->frame_count);
  self->pixels = std::move(pixels);
  self->use_fbo = use_fbo;
  return self;
}

#define SYNTHETIC_TEXTURE_PLUGIN(obj)                                        \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), synthetic_texture_plugin_get_type(),    \
                              SyntheticTexturePlugin))

struct _SyntheticTexturePlugin {
  GObject parent_instance;
  FlTextureRegistrar* texture_registrar;
  FlMethodChannel* channel;
  GHashTable* textures;
};

G_DEFINE_TYPE(SyntheticTexturePlugin, synthetic_texture_plugin, g_object_get_type())

static void synthetic_texture_plugin_dispose(GObject* object) {
  SyntheticTexturePlugin* self = SYNTHETIC_TEXTURE_PLUGIN(object);
  g_clear_object(&self->channel);
  g_clear_pointer(&self->textures, g_hash_table_unref);
  G_OBJECT_CLASS(synthetic_texture_plugin_parent_class)->dispose(object);
}

static FlMethodResponse* synthetic_texture_create(SyntheticTexturePlugin* self,
                                                  FlValue* args) {
  FlValue* width_value = fl_value_lookup_string(args, "width");
  FlValue* height_value = fl_value_lookup_string(args, "height");
  if (width_value == nullptr || height_value == nullptr ||
      fl_value_get_type(width_value) != FL_VALUE_TYPE_INT ||
      fl_value_get_type(height_value) != FL_VALUE_TYPE_INT) {
    return FL_METHOD_RESPONSE(fl_method_error_response_new(
        "invalid_args", "width and height must be integers", nullptr));
  }

  const gint width = static_cast<gint>(fl_value_get_int(width_value));
  const gint height = static_cast<gint>(fl_value_get_int(height_value));
  if (width <= 0 || height <= 0) {
    return FL_METHOD_RESPONSE(fl_method_error_response_new(
        "invalid_args", "width and height must be positive", nullptr));
  }

  FlTextureRegistrar* registrar = self->texture_registrar;
  const gboolean use_fbo = read_use_fbo_arg(args);
  SyntheticPatternTexture* texture =
      synthetic_pattern_texture_new(registrar, width, height, use_fbo);
  if (!fl_texture_registrar_register_texture(registrar,
                                             FL_TEXTURE(texture))) {
    g_object_unref(texture);
    return FL_METHOD_RESPONSE(fl_method_error_response_new(
        "register_failed", "Failed to register FlTextureGL", nullptr));
  }

  const int64_t texture_id =
      static_cast<int64_t>(fl_texture_get_id(FL_TEXTURE(texture)));
  g_hash_table_insert(self->textures, GINT_TO_POINTER(texture_id), texture);

  g_autoptr(FlValue) result = fl_value_new_map();
  fl_value_set_string_take(result, "textureId",
                           fl_value_new_int(texture_id));
  fl_value_set_string_take(result, "width", fl_value_new_int(width));
  fl_value_set_string_take(result, "height", fl_value_new_int(height));
  return FL_METHOD_RESPONSE(fl_method_success_response_new(result));
}

static FlMethodResponse* synthetic_texture_create_from_file(
    SyntheticTexturePlugin* self,
    FlValue* args) {
  FlValue* path_value = fl_value_lookup_string(args, "path");
  if (path_value == nullptr ||
      fl_value_get_type(path_value) != FL_VALUE_TYPE_STRING) {
    return FL_METHOD_RESPONSE(fl_method_error_response_new(
        "invalid_args", "path must be a string", nullptr));
  }

  RgbaSequenceHeader header{};
  std::vector<uint8_t> pixels;
  const char* path = fl_value_get_string(path_value);
  if (!read_rgba_sequence_file(path, &header, &pixels)) {
    return FL_METHOD_RESPONSE(fl_method_error_response_new(
        "invalid_file",
        "Expected RGBQ header + RGBA payload (run tool/extract_rgba_frames.sh)",
        nullptr));
  }

  FlTextureRegistrar* registrar = self->texture_registrar;
  const gboolean use_fbo = read_use_fbo_arg(args);
  SyntheticRgbaSequenceTexture* texture =
      synthetic_rgba_sequence_texture_new(&header, std::move(pixels), use_fbo);
  if (!fl_texture_registrar_register_texture(registrar,
                                             FL_TEXTURE(texture))) {
    g_object_unref(texture);
    return FL_METHOD_RESPONSE(fl_method_error_response_new(
        "register_failed", "Failed to register FlTextureGL", nullptr));
  }

  const int64_t texture_id =
      static_cast<int64_t>(fl_texture_get_id(FL_TEXTURE(texture)));
  g_hash_table_insert(self->textures, GINT_TO_POINTER(texture_id), texture);

  g_autoptr(FlValue) result = fl_value_new_map();
  fl_value_set_string_take(result, "textureId",
                           fl_value_new_int(texture_id));
  fl_value_set_string_take(result, "width",
                           fl_value_new_int(static_cast<int64_t>(header.width)));
  fl_value_set_string_take(result, "height",
                           fl_value_new_int(static_cast<int64_t>(header.height)));
  fl_value_set_string_take(
      result, "frameCount",
      fl_value_new_int(static_cast<int64_t>(header.frame_count)));
  return FL_METHOD_RESPONSE(fl_method_success_response_new(result));
}

static FlMethodResponse* synthetic_texture_create_yuv(SyntheticTexturePlugin* self,
                                                      FlValue* args) {
  FlValue* width_value = fl_value_lookup_string(args, "width");
  FlValue* height_value = fl_value_lookup_string(args, "height");
  if (width_value == nullptr || height_value == nullptr ||
      fl_value_get_type(width_value) != FL_VALUE_TYPE_INT ||
      fl_value_get_type(height_value) != FL_VALUE_TYPE_INT) {
    return FL_METHOD_RESPONSE(fl_method_error_response_new(
        "invalid_args", "width and height must be integers", nullptr));
  }

  const gint width = static_cast<gint>(fl_value_get_int(width_value));
  const gint height = static_cast<gint>(fl_value_get_int(height_value));
  if (width <= 0 || height <= 0 || (width % 2) != 0 || (height % 2) != 0) {
    return FL_METHOD_RESPONSE(fl_method_error_response_new(
        "invalid_args", "width and height must be positive even integers",
        nullptr));
  }

  FlTextureRegistrar* registrar = self->texture_registrar;
  SyntheticYuvPatternTexture* texture =
      synthetic_yuv_pattern_texture_new(width, height);
  if (!fl_texture_registrar_register_texture(registrar,
                                             FL_TEXTURE(texture))) {
    g_object_unref(texture);
    return FL_METHOD_RESPONSE(fl_method_error_response_new(
        "register_failed", "Failed to register FlTextureGL", nullptr));
  }

  const int64_t texture_id =
      static_cast<int64_t>(fl_texture_get_id(FL_TEXTURE(texture)));
  g_hash_table_insert(self->textures, GINT_TO_POINTER(texture_id), texture);

  g_autoptr(FlValue) result = fl_value_new_map();
  fl_value_set_string_take(result, "textureId",
                           fl_value_new_int(texture_id));
  fl_value_set_string_take(result, "width", fl_value_new_int(width));
  fl_value_set_string_take(result, "height", fl_value_new_int(height));
  return FL_METHOD_RESPONSE(fl_method_success_response_new(result));
}

static FlMethodResponse* synthetic_texture_mark_frame_available(
    SyntheticTexturePlugin* self,
    FlValue* args) {
  FlValue* texture_id_value = fl_value_lookup_string(args, "textureId");
  if (texture_id_value == nullptr ||
      fl_value_get_type(texture_id_value) != FL_VALUE_TYPE_INT) {
    return FL_METHOD_RESPONSE(fl_method_error_response_new(
        "invalid_args", "textureId must be an integer", nullptr));
  }

  const int64_t texture_id = fl_value_get_int(texture_id_value);
  FlTexture* texture = FL_TEXTURE(g_hash_table_lookup(
      self->textures, GINT_TO_POINTER(texture_id)));
  if (texture == nullptr) {
    return FL_METHOD_RESPONSE(fl_method_error_response_new(
        "not_found", "Unknown textureId", nullptr));
  }

  FlTextureRegistrar* registrar = self->texture_registrar;
  fl_texture_registrar_mark_texture_frame_available(registrar,
                                                    FL_TEXTURE(texture));
  return FL_METHOD_RESPONSE(fl_method_success_response_new(fl_value_new_bool(true)));
}

static FlMethodResponse* synthetic_texture_dispose_texture(
    SyntheticTexturePlugin* self,
    FlValue* args) {
  FlValue* texture_id_value = fl_value_lookup_string(args, "textureId");
  if (texture_id_value == nullptr ||
      fl_value_get_type(texture_id_value) != FL_VALUE_TYPE_INT) {
    return FL_METHOD_RESPONSE(fl_method_error_response_new(
        "invalid_args", "textureId must be an integer", nullptr));
  }

  const int64_t texture_id = fl_value_get_int(texture_id_value);
  FlTexture* texture = FL_TEXTURE(g_hash_table_lookup(
      self->textures, GINT_TO_POINTER(texture_id)));
  if (texture == nullptr) {
    return FL_METHOD_RESPONSE(fl_method_success_response_new(fl_value_new_bool(true)));
  }

  FlTextureRegistrar* registrar = self->texture_registrar;
  fl_texture_registrar_unregister_texture(registrar, texture);
  g_hash_table_remove(self->textures, GINT_TO_POINTER(texture_id));
  return FL_METHOD_RESPONSE(fl_method_success_response_new(fl_value_new_bool(true)));
}

static void synthetic_texture_plugin_handle_method_call(
    SyntheticTexturePlugin* self,
    FlMethodCall* method_call) {
  g_autoptr(FlMethodResponse) response = nullptr;
  const gchar* method = fl_method_call_get_name(method_call);
  FlValue* args = fl_method_call_get_args(method_call);

  if (g_strcmp0(method, "create") == 0) {
    response = synthetic_texture_create(self, args);
  } else if (g_strcmp0(method, "createFromFile") == 0) {
    response = synthetic_texture_create_from_file(self, args);
  } else if (g_strcmp0(method, "createYuv") == 0) {
    response = synthetic_texture_create_yuv(self, args);
  } else if (g_strcmp0(method, "markFrameAvailable") == 0) {
    response = synthetic_texture_mark_frame_available(self, args);
  } else if (g_strcmp0(method, "dispose") == 0) {
    response = synthetic_texture_dispose_texture(self, args);
  } else {
    response = FL_METHOD_RESPONSE(fl_method_not_implemented_response_new());
  }

  g_autoptr(GError) error = nullptr;
  if (!fl_method_call_respond(method_call, response, &error)) {
    g_warning("Failed to send method call response: %s", error->message);
  }
}

static void synthetic_texture_plugin_class_init(
    SyntheticTexturePluginClass* klass) {
  G_OBJECT_CLASS(klass)->dispose = synthetic_texture_plugin_dispose;
}

static void synthetic_texture_plugin_init(SyntheticTexturePlugin* self) {
  self->textures = g_hash_table_new_full(g_direct_hash, g_direct_equal, nullptr,
                                         g_object_unref);
}

static SyntheticTexturePlugin* synthetic_texture_plugin_new(
    FlPluginRegistrar* registrar) {
  SyntheticTexturePlugin* self = SYNTHETIC_TEXTURE_PLUGIN(
      g_object_new(synthetic_texture_plugin_get_type(), nullptr));
  self->texture_registrar =
      fl_plugin_registrar_get_texture_registrar(registrar);

  g_autoptr(FlStandardMethodCodec) codec = fl_standard_method_codec_new();
  self->channel = fl_method_channel_new(
      fl_plugin_registrar_get_messenger(registrar),
      "synthetic_texture", FL_METHOD_CODEC(codec));
  fl_method_channel_set_method_call_handler(
      self->channel,
      [](FlMethodChannel* channel, FlMethodCall* method_call,
         gpointer user_data) {
        synthetic_texture_plugin_handle_method_call(
            SYNTHETIC_TEXTURE_PLUGIN(user_data), method_call);
      },
      g_object_ref(self), g_object_unref);
  return self;
}

void synthetic_texture_plugin_register_with_registrar(
    FlPluginRegistrar* registrar) {
  SyntheticTexturePlugin* plugin = synthetic_texture_plugin_new(registrar);
  g_object_unref(plugin);
}
