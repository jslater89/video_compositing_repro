#ifndef SYNTHETIC_YUV_PATTERN_TEXTURE_H_
#define SYNTHETIC_YUV_PATTERN_TEXTURE_H_

#include <flutter_linux/flutter_linux.h>

G_BEGIN_DECLS

#define SYNTHETIC_YUV_PATTERN_TEXTURE_TYPE \
  (synthetic_yuv_pattern_texture_get_type())

G_DECLARE_FINAL_TYPE(SyntheticYuvPatternTexture,
                     synthetic_yuv_pattern_texture,
                     SYNTHETIC,
                     YUV_PATTERN_TEXTURE,
                     FlTextureGL)

SyntheticYuvPatternTexture* synthetic_yuv_pattern_texture_new(gint width,
                                                              gint height);

G_END_DECLS

#endif  // SYNTHETIC_YUV_PATTERN_TEXTURE_H_
