#pragma once
#include "application/model.h"

namespace life_demo::shaders {
inline grassland::rhi::BindingLayoutDesc DrawLayout() {
  return {{{0, rhi::BINDING_BYTE_ADDRESS_BUFFER},
           {1, rhi::BINDING_UNIFORM_BUFFER},
           {2, rhi::BINDING_BYTE_ADDRESS_BUFFER},
           {3, rhi::BINDING_BYTE_ADDRESS_BUFFER}}};
}

inline grassland::rhi::BindingLayoutDesc ResolveLayout() {
  return {{{0, rhi::BINDING_UNIFORM_BUFFER}, {1, rhi::BINDING_TEXTURE2D}, {2, rhi::BINDING_TEXTURE2D}}};
}

inline std::vector<grassland::rhi::InputAttributeDesc> DrawAttributes() {
  return {{0, 0, rhi::INPUT_TYPE_FLOAT2, offsetof(Vertex, position)},
          {1, 0, rhi::INPUT_TYPE_FLOAT4, offsetof(Vertex, color)},
          {2, 1, rhi::INPUT_TYPE_UINT, 0}};
}
}  // namespace life_demo::shaders
