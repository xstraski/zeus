#ifndef ZEUS_MATH_H
#define ZEUS_MATH_H

#include "zeus_platform.h"

// NOTE(ivan): Basic 2D vector.
union v2 {
	struct {
		f32 X;
		f32 Y;
	};
	struct {
		f32 U;
		f32 V;
	};
	f32 E[2];
};

inline v2
MakeV2(f32 X, f32 Y) {
	v2 Result;

	Result.X = X;
	Result.Y = Y;

	return Result;
}

// NOTE(ivan): Basic 3D vector.
union v3 {
	struct {
		f32 X;
		f32 Y;
		f32 Z;
	};
	struct {
		f32 R;
		f32 G;
		f32 B;
	};
	f32 E[3];
};

inline v3
MakeV3(f32 X, f32 Y, f32 Z) {
	v3 Result;

	Result.X = X;
	Result.Y = Y;
	Result.Z = Z;

	return Result;
}

// NOTE(ivan): Basic 4D vector.
union v4 {
	struct {
		f32 X;
		f32 Y;
		f32 Z;
		f32 W;
	};
	struct {
		f32 R;
		f32 G;
		f32 B;
		f32 A;
	};
	f32 E[4];
};

inline v4
MakeV4(f32 X, f32 Y, f32 Z, f32 W) {
	v4 Result;

	Result.X = X;
	Result.Y = Y;
	Result.Z = Z;
	Result.W = W;

	return Result;
}

// NOTE(ivan): Aliases for color.
typedef v3 rgb;
typedef v4 rgba;

inline rgb
MakeRGB(f32 R, f32 G, f32 B) {
	rgb Result;

	Result.R = R;
	Result.G = G;
	Result.B = B;

	return Result;
}
inline rgba
MakeRGBA(f32 R, f32 G, f32 B, f32 A) {
	rgba Result;

	Result.R = R;
	Result.G = G;
	Result.B = B;
	Result.A = A;

	return Result;
}

#endif // #ifndef ZEUS_MATH_H
