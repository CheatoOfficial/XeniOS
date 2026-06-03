#include <metal_stdlib>

using namespace metal;

struct TextureUploadRepackConstants {
  uint source_offset;
  uint dest_offset;
  uint source_row_pitch;
  uint dest_row_pitch;
  uint source_image_pitch;
  uint dest_image_pitch;
  uint row_bytes;
  uint row_count;
  uint depth;
  uint padding0;
  uint padding1;
  uint padding2;
};

kernel void entry_xe(constant TextureUploadRepackConstants& constants
                         [[buffer(0)]],
                     const device uchar* source [[buffer(1)]],
                     device uchar* dest [[buffer(2)]],
                     uint3 gid [[thread_position_in_grid]]) {
  uint row_offset = gid.x << 2u;
  if (row_offset >= constants.row_bytes || gid.y >= constants.row_count ||
      gid.z >= constants.depth) {
    return;
  }

  uint source_offset = constants.source_offset +
                       gid.z * constants.source_image_pitch +
                       gid.y * constants.source_row_pitch + row_offset;
  uint dest_offset = constants.dest_offset + gid.z * constants.dest_image_pitch +
                     gid.y * constants.dest_row_pitch + row_offset;

  dest[dest_offset] = source[source_offset];
  if (row_offset + 1u < constants.row_bytes) {
    dest[dest_offset + 1u] = source[source_offset + 1u];
  }
  if (row_offset + 2u < constants.row_bytes) {
    dest[dest_offset + 2u] = source[source_offset + 2u];
  }
  if (row_offset + 3u < constants.row_bytes) {
    dest[dest_offset + 3u] = source[source_offset + 3u];
  }
}
