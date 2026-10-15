/*
 * VOBGArt.h -- colours and cached block rendering.
 *
 * Drawing a gradient-filled, rounded, highlighted block directly for all ~200
 * cells every frame is far too slow on the hardware this app targets (iOS 6
 * devices are 32-bit, and some are an iPhone 4S). So each block is rendered
 * once per (piece, size) into an offscreen bitmap and then blitted. The frame
 * loop only issues cheap image draws.
 */

#ifndef VOBG_ART_H
#define VOBG_ART_H

#import <UIKit/UIKit.h>

/* Neon-on-black palette. */
extern UIColor *VOBGColorForPiece(int piece);
extern UIColor *VOBGCellColor(int piece);

@interface VOBGArt : NSObject

/* Shared cache. */
+ (VOBGArt *)sharedArt;

/* Blitted block image for a piece at the given edge length in points. */
- (UIImage *)blockImageForPiece:(int)piece size:(CGFloat)size;

/* Drop the cache, e.g. when the cell size changes on rotation. */
- (void)invalidate;

@end

#endif /* VOBG_ART_H */