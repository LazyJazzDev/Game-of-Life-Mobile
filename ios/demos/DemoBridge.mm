#import "DemoBridge.h"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <limits>
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include "DemoSession.h"
#include "RenderQueue.h"
#include "grassland/graphics/backend/metal/metal_core.h"
#include "grassland/graphics/backend/metal/metal_image.h"

@implementation DemoRenderer {
  MTKView *_view;
  DemoProgress _progress;
  std::atomic<uint64_t> _generation;
  BOOL _active, _busy, _failed, _filePending;
  CFTimeInterval _statsTime;
  NSInteger _statsFrames;
  // Owned and used only on LongMarchRenderQueue.
  std::unique_ptr<DemoSession> _session;
  id<MTLRenderPipelineState> _presentPipeline;
  NSInteger _frames;
  BOOL _gameSleeping;
  uint64_t _wakeRevision, _inputRevision;
  NSInteger _idleSmokeStage;
  std::atomic<int> _presentInFlight;
  double _benchmarkSeconds, _benchmarkRenderSeconds, _benchmarkStart;
}

- (instancetype)init {
  if ((self = [super init])) {
    _generation = 0;
    _presentInFlight = 0;
  }
  return self;
}

- (void)startView:(MTKView *)view resources:(NSURL *)resources progress:(DemoProgress)progress {
  NSAssert(NSThread.isMainThread, @"Configure game views on main");
  uint64_t generation = ++_generation;
  _view = view;
  _progress = [progress copy];
  _gameSleeping = NO;
  _idleSmokeStage = 0;
  _benchmarkSeconds = _benchmarkRenderSeconds = _benchmarkStart = 0;
  ++_wakeRevision;
  _failed = NO;
  _filePending = NO;
  _statsTime = 0;
  _statsFrames = 0;
  view.device = MTLCreateSystemDefaultDevice();
  view.colorPixelFormat = MTLPixelFormatBGRA8Unorm;
  view.framebufferOnly = YES;
  view.preferredFramesPerSecond = 60;
  view.delegate = self;
  // Frames are scheduled on demand (scheduleGameFrame:), not by a running display link.
  view.paused = YES;
  [self wakeGame];
  dispatch_async(LongMarchRenderQueue(), ^{
    @autoreleasepool {
      if (self->_generation != generation)
        return;
      self->_session.reset();
      self->_presentPipeline = nil;
      self->_frames = 0;
      try {
        self->_session = std::make_unique<DemoSession>(resources.fileSystemRepresentation);
        self->_session->Game()->EnableNativeSizeControls();
        auto core = static_cast<grassland::graphics::backend::MetalCore *>(self->_session->Core());
        id<MTLDevice> device = (__bridge id<MTLDevice>)core->Device();
        NSString *source =
            @"#include <metal_stdlib>\nusing namespace metal;\n"
             "struct V{float4 p [[position]];float2 uv;};\n"
             "vertex V present_vertex(uint i [[vertex_id]]){float2 p=float2((i<<1)&2,i&2);return "
             "{float4(p*float2(2,-2)+float2(-1,1),0,1),p};}\n"
             "fragment float4 present_fragment(V v [[stage_in]],texture2d<float> image [[texture(0)]]){"
             "constexpr sampler s(coord::normalized,address::clamp_to_edge,filter::linear);"
             "return float4(saturate(image.sample(s,v.uv).rgb),1);}";
        NSError *error = nil;
        id<MTLLibrary> library = [device newLibraryWithSource:source options:nil error:&error];
        if (!library)
          throw std::runtime_error(error.localizedDescription.UTF8String);
        MTLRenderPipelineDescriptor *descriptor = [MTLRenderPipelineDescriptor new];
        descriptor.vertexFunction = [library newFunctionWithName:@"present_vertex"];
        descriptor.fragmentFunction = [library newFunctionWithName:@"present_fragment"];
        descriptor.colorAttachments[0].pixelFormat = MTLPixelFormatBGRA8Unorm;
        self->_presentPipeline = [device newRenderPipelineStateWithDescriptor:descriptor error:&error];
        if (!self->_presentPipeline)
          throw std::runtime_error(error.localizedDescription.UTF8String);
      } catch (const std::exception &error) {
        NSString *failure = [NSString stringWithUTF8String:error.what()];
        self->_session.reset();
        dispatch_async(dispatch_get_main_queue(), ^{
          if (self->_generation == generation) {
            self->_failed = YES;
            progress(0, 0, 0, @"—", 0, 0, 0, failure);
          }
        });
      }
    }
  });
}

// Use the display link for animation pacing, and one-shot timers only for
// slower simulation deadlines. Dispatch timers are not accurate frame clocks.
- (void)scheduleGameFrame:(double)delay {
  uint64_t wake = ++_wakeRevision;
  if (!_active || !std::isfinite(delay)) {
    _view.paused = YES;
    return;
  }
  if (delay <= 1.0 / 60.0) {
    _view.paused = NO;
    return;
  }
  _view.paused = YES;
  dispatch_after(dispatch_time(DISPATCH_TIME_NOW, int64_t(delay * NSEC_PER_SEC)), dispatch_get_main_queue(), ^{
    if (wake == self->_wakeRevision && self->_active && !self->_busy)
      [self->_view draw];
  });
}

- (void)wakeGame {
  ++_inputRevision;
  [self scheduleGameFrame:0];
}

- (void)setGridDimension:(NSInteger)axis value:(NSInteger)value {
  const uint64_t generation = _generation;
  [self wakeGame];
  dispatch_async(LongMarchRenderQueue(), ^{
    if (self->_generation == generation && self->_session && self->_session->Game())
      self->_session->Game()->SetGridDimension(int(axis), int(value));
  });
}

- (void)setGameBottomControlInset:(float)heightFraction {
  const uint64_t generation = _generation;
  [self wakeGame];
  dispatch_async(LongMarchRenderQueue(), ^{
    if (self->_generation == generation && self->_session && self->_session->Game())
      self->_session->Game()->SetBottomControlInset(heightFraction);
  });
}

- (void)setGameCutoutInsetsLeft:(float)left top:(float)top right:(float)right {
  const uint64_t generation = _generation;
  [self wakeGame];
  dispatch_async(LongMarchRenderQueue(), ^{
    if (self->_generation == generation && self->_session && self->_session->Game())
      self->_session->Game()->SetCutoutInsets(left, top, right);
  });
}

- (void)setGameControlExtentLimit:(float)heightFraction {
  const uint64_t generation = _generation;
  [self wakeGame];
  dispatch_async(LongMarchRenderQueue(), ^{
    if (self->_generation == generation && self->_session && self->_session->Game())
      self->_session->Game()->SetControlExtentLimit(heightFraction);
  });
}

- (void)setGameIconRotation:(float)radians {
  const uint64_t generation = _generation;
  [self wakeGame];
  dispatch_async(LongMarchRenderQueue(), ^{
    if (self->_generation == generation && self->_session && self->_session->Game())
      self->_session->Game()->SetIconOrientation(radians);
  });
}

- (void)input:(NSInteger)kind x:(double)x y:(double)y value:(double)value {
  uint64_t generation = _generation;
  BOOL resetClock = _gameSleeping;
  _gameSleeping = NO;
  [self wakeGame];
  dispatch_async(LongMarchRenderQueue(), ^{
    if (self->_generation != generation || !self->_session || !self->_session->Game())
      return;
    self->_session->Game()->PrepareInput(resetClock);
    auto *window = self->_session->Game()->Window();
    auto size = window->GetFramebufferSize();
    if (kind <= 3 || kind == 6)
      window->SendPointer(x * size.x, y * size.y);
    if (kind == 1) {
      window->CursorEnterEvent().InvokeCallbacks(true);
      window->SendMouseButton(int(value), GLFW_PRESS);
    }
    if (kind == 2) {
      window->SendMouseButton(int(value), GLFW_RELEASE);
      window->CursorEnterEvent().InvokeCallbacks(false);
    }
    if (kind == 4) {
      window->SendKey(int(value), GLFW_PRESS);
      window->SendKey(int(value), GLFW_RELEASE);
    }
    if (kind == 5) {
      window->SendFocus(value != 0);
      if (value)
        self->_session->Game()->ResetClock();
    }
    if (kind == 6)
      window->MagnifyEvent().InvokeCallbacks(grassland::graphics::MagnifyGesture{
          value, x * size.x, y * size.y, grassland::graphics::MagnifyPhase::kUpdate});
  });
}

- (void)completeFile:(NSString *)path completion:(void (^)(NSString *))completion {
  uint64_t generation = _generation;
  dispatch_async(LongMarchRenderQueue(), ^{
    std::string error;
    if (self->_generation == generation && self->_session && self->_session->Game())
      error = self->_session->Game()->CompleteFile(path.UTF8String);
    NSString *message = error.empty() ? nil : [NSString stringWithUTF8String:error.c_str()];
    dispatch_async(dispatch_get_main_queue(), ^{
      self->_filePending = NO;
      [self wakeGame];
      completion(message);
    });
  });
}

- (void)setActive:(BOOL)active {
  if (_active == active)
    return;
  if (_active != active)
    [self input:5 x:0 y:0 value:active];
  if (_active != active)
    _statsTime = 0;
  _active = active;
  _view.paused = YES;
  [self wakeGame];
}

- (void)mtkView:(MTKView *)view drawableSizeWillChange:(CGSize)size {
  [self wakeGame];
}

- (void)drawInMTKView:(MTKView *)view {
  if (!_active || _busy || _failed)
    return;
  if (_presentInFlight >= 2) {
    [self scheduleGameFrame:1.0 / 120.0];
    return;
  }
  id<CAMetalDrawable> drawable = view.currentDrawable;
  if (!drawable) {
    [self scheduleGameFrame:0.1];
    return;
  }
  _busy = YES;
  uint64_t generation = _generation;
  const uint64_t inputRevision = _inputRevision;
  // The game renders at the drawable's native resolution.
  const int width = std::max(1, int(view.drawableSize.width));
  const int height = std::max(1, int(view.drawableSize.height));
  DemoProgress progress = _progress;
  dispatch_async(LongMarchRenderQueue(), ^{
    @autoreleasepool {
      NSString *failure = nil, *deviceName = @"—";
      double seconds = 0;
      double nextFrameDelay = 0;
      NSInteger frames = 0;
      try {
        if (self->_generation == generation && self->_session) {
          auto start = std::chrono::steady_clock::now();
          self->_session->Resize(width, height);
          self->_session->Render();
          const double renderSeconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
          {
            auto game = self->_session->Game();
            nextFrameDelay = game->NextFrameDelay();
            int action = game->FileRequest();
            const auto sizeRequest = game->TakeSizeControlRequest();
            const auto sizeBounds = sizeRequest.x ? game->SizeControlBounds(sizeRequest.x) : glm::vec4{0.0f};
            const CGRect sizeRect =
                CGRectMake(sizeBounds.x, sizeBounds.y, sizeBounds.z - sizeBounds.x, sizeBounds.w - sizeBounds.y);
            dispatch_async(dispatch_get_main_queue(), ^{
              if (self->_generation == generation && action && !self->_filePending && self.fileRequest) {
                self->_filePending = YES;
                self.fileRequest(action);
              }
              if (self->_generation == generation && sizeRequest.x && self.sizeRequest)
                self.sizeRequest(sizeRequest.x, sizeRequest.y, sizeRect);
            });
          }
          auto core = static_cast<grassland::graphics::backend::MetalCore *>(self->_session->Core());
          auto image = static_cast<grassland::graphics::backend::MetalImage *>(self->_session->Image());
          id<MTLCommandQueue> queue = (__bridge id<MTLCommandQueue>)core->Queue();
          id<MTLCommandBuffer> command = [queue commandBuffer];
          MTLRenderPassDescriptor *pass = [MTLRenderPassDescriptor renderPassDescriptor];
          pass.colorAttachments[0].texture = drawable.texture;
          pass.colorAttachments[0].loadAction = MTLLoadActionClear;
          pass.colorAttachments[0].clearColor = MTLClearColorMake(0, 0, 0, 1);
          pass.colorAttachments[0].storeAction = MTLStoreActionStore;
          id<MTLRenderCommandEncoder> encoder = [command renderCommandEncoderWithDescriptor:pass];
          double fit = std::min(double(drawable.texture.width) / width, double(drawable.texture.height) / height);
          [encoder
              setViewport:MTLViewport{(drawable.texture.width - width * fit) / 2,
                                      (drawable.texture.height - height * fit) / 2, width * fit, height * fit, 0, 1}];
          [encoder setRenderPipelineState:self->_presentPipeline];
          [encoder setFragmentTexture:(__bridge id<MTLTexture>)image->Handle() atIndex:0];
          [encoder drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:3];
          [encoder endEncoding];
          [command presentDrawable:drawable];
          // The presentation and game commands share one ordered Metal queue.
          // The next frame may update CPU state while this one is presenting;
          // Metal retains the encoded textures/pipeline until completion.
          ++self->_presentInFlight;
          [command addCompletedHandler:^(id<MTLCommandBuffer> completed) {
            --self->_presentInFlight;
            if (completed.error) {
              NSString *message = completed.error.localizedDescription;
              dispatch_async(dispatch_get_main_queue(), ^{
                if (self->_generation == generation) {
                  self->_failed = YES;
                  progress(0, 0, 0, @"—", width, height, 0, message);
                }
              });
            }
          }];
          [command commit];
          seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
          frames = ++self->_frames;
          if (frames == 1 && NSProcessInfo.processInfo.environment[@"LONGMARCH_SMOKE_AUTORUN"]) {
            dispatch_after(dispatch_time(DISPATCH_TIME_NOW, NSEC_PER_SEC), LongMarchRenderQueue(), ^{
              if (self->_generation != generation)
                return;
              NSInteger before = self->_frames;
              dispatch_after(dispatch_time(DISPATCH_TIME_NOW, 2 * NSEC_PER_SEC), LongMarchRenderQueue(), ^{
                if (self->_generation != generation)
                  return;
                NSDictionary *result =
                    @{@"frames_before" : @(before),
                      @"frames_after" : @(self->_frames),
                      @"seconds" : @2};
                NSURL *url = [NSFileManager.defaultManager URLsForDirectory:NSDocumentDirectory
                                                                  inDomains:NSUserDomainMask]
                                 .firstObject;
                [[NSJSONSerialization dataWithJSONObject:result options:NSJSONWritingPrettyPrinted error:nil]
                    writeToURL:[url URLByAppendingPathComponent:@"AutorunSmoke.json"]
                    atomically:YES];
              });
            });
          }
          if (NSProcessInfo.processInfo.environment[@"LONGMARCH_SMOKE_BENCHMARK"]) {
            if (frames < 70)
              nextFrameDelay = 0;
            if (frames == 10)
              self->_benchmarkStart = CACurrentMediaTime();
            if (frames > 10 && frames <= 70) {
              self->_benchmarkSeconds += seconds;
              self->_benchmarkRenderSeconds += renderSeconds;
            }
            if (frames == 70) {
              NSDictionary *result = @{
                @"demo" : @"gol",
                @"frames" : @60,
                @"width" : @(width),
                @"height" : @(height),
                @"mean_submit_ms" : @(self->_benchmarkSeconds * 1000 / 60),
                @"mean_render_submit_ms" : @(self->_benchmarkRenderSeconds * 1000 / 60),
                @"fps" : @(60 / (CACurrentMediaTime() - self->_benchmarkStart))
              };
              NSURL *url = [NSFileManager.defaultManager URLsForDirectory:NSDocumentDirectory
                                                                inDomains:NSUserDomainMask]
                               .firstObject;
              [[NSJSONSerialization dataWithJSONObject:result options:NSJSONWritingPrettyPrinted error:nil]
                  writeToURL:[url URLByAppendingPathComponent:@"BenchmarkResult.json"]
                  atomically:YES];
            }
          }
          deviceName = [NSString stringWithUTF8String:core->DeviceName().c_str()];
          if ((frames == 3 || !std::isfinite(nextFrameDelay)) &&
              NSProcessInfo.processInfo.environment[@"LONGMARCH_SMOKE_DEMO"]) {
            NSDictionary *result = @{
              @"demo" : @"gol",
              @"width" : @(width),
              @"height" : @(height),
              @"frames" : @(frames),
              @"idle" : @(!std::isfinite(nextFrameDelay)),
              @"drawable_pixel_format" : @(drawable.texture.pixelFormat),
              @"device" : deviceName,
              @"frame_seconds" : @(seconds)
            };
            NSURL *url = [NSFileManager.defaultManager URLsForDirectory:NSDocumentDirectory inDomains:NSUserDomainMask]
                             .firstObject;
            [[NSJSONSerialization dataWithJSONObject:result options:NSJSONWritingPrettyPrinted error:nil]
                writeToURL:[url URLByAppendingPathComponent:@"DemoSmokeResult.json"]
                atomically:YES];
          }
        }
      } catch (const std::exception &error) {
        failure = [NSString stringWithUTF8String:error.what()];
        self->_session.reset();
      }
      dispatch_async(dispatch_get_main_queue(), ^{
        self->_busy = NO;
        if (self->_generation == generation) {
          if (failure)
            self->_failed = YES;
          if (!failure) {
            bool inputPending = inputRevision != self->_inputRevision;
            self->_gameSleeping = !inputPending && !std::isfinite(nextFrameDelay);
            double delay = inputPending ? 0 : std::max(1.0 / 60.0, nextFrameDelay) - seconds;
            [self scheduleGameFrame:delay];
            if (self->_gameSleeping && self->_idleSmokeStage < 2 &&
                NSProcessInfo.processInfo.environment[@"LONGMARCH_SMOKE_IDLE"]) {
              NSInteger stage = self->_idleSmokeStage++;
              dispatch_after(dispatch_time(DISPATCH_TIME_NOW, 2 * NSEC_PER_SEC), LongMarchRenderQueue(), ^{
                if (self->_generation != generation)
                  return;
                NSDictionary *result = @{
                  @"demo" : @"gol",
                  @"frames_before" : @(frames),
                  @"frames_after" : @(self->_frames),
                  @"seconds" : @2
                };
                NSURL *url = [NSFileManager.defaultManager URLsForDirectory:NSDocumentDirectory
                                                                  inDomains:NSUserDomainMask]
                                 .firstObject;
                [[NSJSONSerialization dataWithJSONObject:result options:NSJSONWritingPrettyPrinted error:nil]
                    writeToURL:[url URLByAppendingPathComponent:[NSString
                                                                    stringWithFormat:@"IdleSmoke%ld.json", (long)stage]]
                    atomically:YES];
                if (stage == 0)
                  dispatch_async(dispatch_get_main_queue(), ^{
                    if (self->_generation != generation)
                      return;
                    [self input:1 x:.5 y:.5 value:0];
                    [self input:2 x:.5 y:.5 value:0];
                  });
              });
            }
          }
          if ((frames > 0 && (frames <= 3 || frames % 15 == 0 || !std::isfinite(nextFrameDelay))) || failure) {
            CFTimeInterval now = CACurrentMediaTime();
            double fps = self->_statsTime > 0 ? (frames - self->_statsFrames) / (now - self->_statsTime) : 0;
            self->_statsTime = now;
            self->_statsFrames = frames;
            progress(seconds, 0, self->_gameSleeping ? 0 : fps, deviceName, width, height, frames, failure);
          }
        }
      });
    }
  });
}

- (void)stop {
  ++_generation;
  ++_wakeRevision;
  _active = NO;
  _view.paused = YES;
  _view.delegate = nil;
  _view = nil;
  _progress = nil;
  dispatch_async(LongMarchRenderQueue(), ^{
    self->_session.reset();
    self->_presentPipeline = nil;
  });
}

@end
