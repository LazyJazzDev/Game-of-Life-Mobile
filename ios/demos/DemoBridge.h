#import <Foundation/Foundation.h>
#import <MetalKit/MetalKit.h>
NS_ASSUME_NONNULL_BEGIN
typedef void (^DemoProgress)(double frameSeconds,
                             double gpuMilliseconds,
                             double fps,
                             NSString *device,
                             NSInteger width,
                             NSInteger height,
                             NSInteger frames,
                             NSString *_Nullable error);
// Drives the hosted Game of Life in a Metal view. Call public methods on the
// main thread; the game and its GPU resources live on LongMarchRenderQueue.
@interface DemoRenderer : NSObject <MTKViewDelegate>
- (void)startView:(MTKView *)view
        resources:(NSURL *)resources
         progress:(DemoProgress)progress NS_SWIFT_NAME(start(view:resources:progress:));
@property(nonatomic, copy, nullable) void (^confirmationRequest)(NSInteger action);
- (void)completeConfirmation:(BOOL)accepted;
- (void)cancelAgentConfirmation;
@property(nonatomic, copy, nullable) void (^fileRequest)(NSInteger action);
// bounds is the tapped slider as fractions of the view.
@property(nonatomic, copy, nullable) void (^sizeRequest)(NSInteger axis, NSInteger value, CGRect bounds);
- (void)setGridDimension:(NSInteger)axis value:(NSInteger)value;
- (void)requestFileAction:(NSInteger)action;
- (void)completeFile:(NSString *)path completion:(void (^)(NSString *_Nullable error))completion;
- (void)input:(NSInteger)kind x:(double)x y:(double)y value:(double)value;
- (void)setGameIconRotation:(float)radians;
- (void)setGameBottomControlInset:(float)heightFraction;
- (void)setGameCutoutInsetsLeft:(float)left top:(float)top right:(float)right;
- (void)setGameControlOcclusions:(NSArray<NSValue *> *)regions;
- (void)setGameControlPixelSize:(float)pixels;
- (void)setGameControlExtentLimit:(float)heightFraction;
- (void)setActive:(BOOL)active;
- (void)agentCommand:(NSString *)json completion:(void (^)(NSString *))completion;
- (void)stop;
@end
NS_ASSUME_NONNULL_END
