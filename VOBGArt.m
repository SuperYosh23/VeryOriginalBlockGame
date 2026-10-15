/*
 * VOBGArt.m -- see VOBGArt.h.
 */

#import "VOBGArt.h"
#import "VOBGSettings.h"

#include "VOBGTetris.h"

UIColor *VOBGColorForPiece(int piece)
{
    switch (piece) {
    case VOBG_PIECE_I: return [UIColor colorWithRed:0.20f green:0.90f blue:0.96f alpha:1.0f];
    case VOBG_PIECE_J: return [UIColor colorWithRed:0.36f green:0.42f blue:0.96f alpha:1.0f];
    case VOBG_PIECE_L: return [UIColor colorWithRed:0.98f green:0.55f blue:0.16f alpha:1.0f];
    case VOBG_PIECE_O: return [UIColor colorWithRed:0.98f green:0.82f blue:0.18f alpha:1.0f];
    case VOBG_PIECE_S: return [UIColor colorWithRed:0.28f green:0.88f blue:0.42f alpha:1.0f];
    case VOBG_PIECE_Z: return [UIColor colorWithRed:0.95f green:0.25f blue:0.42f alpha:1.0f];
    case VOBG_PIECE_T: return [UIColor colorWithRed:0.72f green:0.34f blue:0.95f alpha:1.0f];
    default: return [UIColor grayColor];
    }
}

/* Same palette, multiplied down for the settled stack so the active piece
 * stands out against it. */
UIColor *VOBGCellColor(int piece)
{
    UIColor *base = VOBGColorForPiece(piece);
    CGFloat r = 0, g = 0, b = 0, a = 1;
    if (![base getRed:&r green:&g blue:&b alpha:&a]) {
        return base;
    }
    return [UIColor colorWithRed:r * 0.72f green:g * 0.72f blue:b * 0.72f alpha:1.0f];
}

@implementation VOBGArt {
    NSMutableDictionary *_cache;
}

+ (VOBGArt *)sharedArt
{
    static VOBGArt *shared = nil;
    static dispatch_once_t once;
    dispatch_once(&once, ^{
        shared = [[VOBGArt alloc] init];
    });
    return shared;
}

- (instancetype)init
{
    self = [super init];
    if (self) {
        _cache = [[NSMutableDictionary alloc] init];
    }
    return self;
}

- (void)invalidate
{
    [_cache removeAllObjects];
}

/* A lit slab: vertical gradient, rounded corners, bright top bevel, dark
 * outline. This is what sells the neon look without any per-frame cost.
 *
 * The style is part of the cache key, not a separate cache: flat blocks are
 * cheaper to draw but not so cheap that two caches are worth the bookkeeping,
 * and switching style must not leave stale glossy art behind.
 */
- (UIImage *)blockImageForPiece:(int)piece size:(CGFloat)size
{
    BOOL glossy = [VOBGSettings sharedSettings].glossyBlocks;
    NSString *key = [NSString stringWithFormat:@"%d-%.1f-%d", piece, size, glossy ? 1 : 0];
    UIImage *cached = [_cache objectForKey:key];
    if (cached != nil) {
        return cached;
    }

    UIColor *base = VOBGColorForPiece(piece);
    CGFloat r, g, b, a;
    [base getRed:&r green:&g blue:&b alpha:&a];

    if (!glossy) {
        return [self flatBlockForPiece:piece size:size r:r g:g b:b key:key];
    }

    CGFloat topR = MIN(1.0, r * 1.0 + 0.22);
    CGFloat topG = MIN(1.0, g * 1.0 + 0.22);
    CGFloat topB = MIN(1.0, b * 1.0 + 0.22);
    CGFloat botR = r * 0.55;
    CGFloat botG = g * 0.55;
    CGFloat botB = b * 0.55;

    /* One pixel of padding so the outline is not clipped at the edge. */
    CGFloat inset = 1.0;
    CGSize canvas = CGSizeMake(size, size);
    UIGraphicsBeginImageContextWithOptions(canvas, NO, 0.0);
    CGContextRef ctx = UIGraphicsGetCurrentContext();

    CGRect box = CGRectInset(CGRectMake(0, 0, size, size), inset, inset);
    CGFloat radius = MAX(2.0, size * 0.16);
    UIBezierPath *path = [UIBezierPath bezierPathWithRoundedRect:box cornerRadius:radius];

    CGContextSaveGState(ctx);
    [path addClip];

    CGColorSpaceRef space = CGColorSpaceCreateDeviceRGB();
    CGFloat comps[8] = {
        topR, topG, topB, 1.0,
        botR, botG, botB, 1.0
    };
    CGFloat locations[2] = {0.0, 1.0};
    CGGradientRef gradient = CGGradientCreateWithColorComponents(space, comps, locations, 2);
    CGContextDrawLinearGradient(ctx, gradient, CGPointMake(0, box.origin.y),
                                CGPointMake(0, CGRectGetMaxY(box)), 0);
    CGGradientRelease(gradient);
    CGColorSpaceRelease(space);
    CGContextRestoreGState(ctx);

    /* Outline. */
    [[UIColor colorWithRed:r * 0.35f green:g * 0.35f blue:b * 0.35f alpha:1.0f] setStroke];
    path.lineWidth = MAX(1.0, size * 0.05);
    [path stroke];

    /* Top bevel highlight: the single cheapest thing that reads as "neon". */
    CGFloat inset2 = size * 0.16;
    CGRect highlight = CGRectInset(box, inset2, inset2);
    highlight.size.height = MAX(1.0, size * 0.12);
    UIBezierPath *gloss = [UIBezierPath bezierPathWithRoundedRect:highlight
                                                     cornerRadius:MAX(1.0, size * 0.05)];
    [[UIColor colorWithWhite:1.0f alpha:0.42f] setFill];
    [gloss fill];

    UIImage *image = UIGraphicsGetImageFromCurrentImageContext();
    UIGraphicsEndImageContext();

    if (image != nil) {
        [_cache setObject:image forKey:key];
    }
    return image;
}

/* Flat style: a single solid fill, square corners, a thin dark border. No
 * gradient and no gloss, which also means one less pass per block -- worth
 * having on the older hardware this ships to. */
- (UIImage *)flatBlockForPiece:(int)piece size:(CGFloat)size
                             r:(CGFloat)r g:(CGFloat)g b:(CGFloat)b
                            key:(NSString *)key
{
    CGFloat inset = 1.0;
    CGContextRef ctx;
    UIBezierPath *path;
    CGRect box = CGRectInset(CGRectMake(0, 0, size, size), inset, inset);
    UIImage *image;

    (void)piece;
    UIGraphicsBeginImageContextWithOptions(CGSizeMake(size, size), NO, 0.0);
    ctx = UIGraphicsGetCurrentContext();

    /* Square corners: that is most of what "flat" reads as. */
    path = [UIBezierPath bezierPathWithRect:box];
    [[UIColor colorWithRed:r green:g blue:b alpha:1.0f] setFill];
    [path fill];

    [[UIColor colorWithRed:r * 0.30f green:g * 0.30f blue:b * 0.30f alpha:1.0f] setStroke];
    path.lineWidth = MAX(1.0, size * 0.06);
    [path stroke];

    image = UIGraphicsGetImageFromCurrentImageContext();
    UIGraphicsEndImageContext();

    if (image != nil) {
        [_cache setObject:image forKey:key];
    }
    return image;
}

@end