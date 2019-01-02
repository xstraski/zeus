#ifndef ZEUS_MATH_H
#define ZEUS_MATH_H

#include "zeus_platform.h"

#define Square(A) ((A) * (A))
#define Cube(A) ((A) * (A) * (A))

// NOTE(ivan): Basic 2D vector.
struct v2 {
	union {
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

	inline v2 &
	operator += (v2 A)
	{
		X += A.X;
		Y += A.Y;

		return *this;
	}
	inline v2 &
	operator += (f32 Value)
	{
		X += Value;
		Y += Value;

		return *this;
	}

	inline v2 &
	operator -= (v2 A)
	{
		X -= A.X;
		Y -= A.Y;

		return *this;
	}
	inline v2 &
	operator -= (f32 Value)
	{
		X -= Value;
		Y -= Value;

		return *this;
	}

	inline v2 &
	operator *= (v2 A)
	{
		X *= A.X;
		Y *= A.Y;

		return *this;
	}
	inline v2 &
	operator *= (f32 Value)
	{
		X *= Value;
		Y *= Value;

		return *this;
	}
};

inline v2
MakeV2(f32 X, f32 Y)
{
	v2 Result;

	Result.X = X;
	Result.Y = Y;

	return Result;
}

inline v2
operator - (v2 A)
{
	v2 Result;

	Result.X = -A.X;
	Result.Y = -A.Y;

	return Result;
}

inline v2
operator + (v2 A, v2 B)
{
	v2 Result;
	
	Result.X = A.X + B.X;
	Result.Y = A.Y + B.Y;
	
	return Result;
}
inline v2
operator + (v2 A, f32 V)
{
	v2 Result;

	Result.X = A.X + V;
	Result.Y = A.Y + V;

	return Result;
}

inline v2
operator - (v2 A, v2 B)
{
	v2 Result;
	
	Result.X = A.X - B.X;
	Result.Y = A.Y - B.Y;
	
	return Result;
}
inline v2
operator - (v2 A, f32 V)
{
	v2 Result;

	Result.X = A.X - V;
	Result.Y = A.Y - V;

	return Result;
}

inline v2
operator * (v2 A, v2 B)
{
	v2 Result;

	Result.X = A.X * B.X;
	Result.Y = A.Y * B.Y;

	return Result;
}
inline v2
operator * (v2 A, f32 V)
{
	v2 Result;

	Result.X = A.X * V;
	Result.Y = A.Y * V;

	return Result;
}

// NOTE(ivan): Basic 3D vector.
struct v3 {
	union {
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

	inline v3 &
	operator += (v3 A)
	{
		X += A.X;
		Y += A.Y;
		Z += A.Z;

		return *this;
	}
	inline v3 &
	operator += (f32 Value)
	{
		X += Value;
		Y += Value;
		Z += Value;

		return *this;
	}

	inline v3 &
	operator -= (v3 A)
	{
		X -= A.X;
		Y -= A.Y;
		Z -= A.Z;

		return *this;
	}
	inline v3 &
	operator -= (f32 Value)
	{
		X -= Value;
		Y -= Value;
		Z -= Value;

		return *this;
	}

	inline v3 &
	operator *= (v3 A)
	{
		X *= A.X;
		Y *= A.Y;
		Z *= A.Z;

		return *this;
	}
	inline v3 &
	operator *= (f32 Value)
	{
		X *= Value;
		Y *= Value;
		Z *= Value;

		return *this;
	}
};

inline v3
MakeV3(f32 X, f32 Y, f32 Z)
{
	v3 Result;

	Result.X = X;
	Result.Y = Y;
	Result.Z = Z;

	return Result;
}

inline v3
operator - (v3 A)
{
	v3 Result;

	Result.X = -A.X;
	Result.Y = -A.Y;
	Result.Z = -A.Z;
	
	return Result;
}

inline v3
operator + (v3 A, v3 B)
{
	v3 Result;

	Result.X = A.X + B.X;
	Result.Y = A.Y + B.Y;
	Result.Z = A.Z + B.Z;

	return Result;
}
inline v3
operator + (v3 A, f32 V)
{
	v3 Result;

	Result.X = A.X + V;
	Result.Y = A.Y + V;
	Result.Z = A.Z + V;

	return Result;
}

inline v3
operator - (v3 A, v3 B)
{
	v3 Result;

	Result.X = A.X - B.X;
	Result.Y = A.Y - B.Y;
	Result.Z = A.Z - B.Z;

	return Result;
}
inline v3
operator - (v3 A, f32 V)
{
	v3 Result;

	Result.X = A.X - V;
	Result.Y = A.Y - V;
	Result.Z = A.Z - V;

	return Result;
}

inline v3
operator * (v3 A, v3 B)
{
	v3 Result;

	Result.X = A.X * B.X;
	Result.Y = A.Y * B.Y;
	Result.Z = A.Z * B.Z;

	return Result;
}
inline v3
operator * (v3 A, f32 V)
{
	v3 Result;

	Result.X = A.X * V;
	Result.Y = A.Y * V;
	Result.Z = A.Z * V;

	return Result;
}

// NOTE(ivan): Basic 4D vector.
struct v4 {
	union {
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

	inline v4 &
	operator += (v4 Other)
	{
		X += Other.X;
		Y += Other.Y;
		Z += Other.Z;
		W += Other.W;

		return *this;
	}
	inline v4 &
	operator += (f32 Value)
	{
		X += Value;
		Y += Value;
		Z += Value;
		W += Value;

		return *this;
	}

	inline v4 &
	operator -= (v4 Other)
	{
		X -= Other.X;
		Y -= Other.Y;
		Z -= Other.Z;
		W -= Other.W;

		return *this;
	}
	inline v4 &
	operator -= (f32 Value)
	{
		X -= Value;
		Y -= Value;
		Z -= Value;
		W -= Value;

		return *this;
	}

	inline v4 &
	operator *= (v4 Other)
	{
		X *= Other.X;
		Y *= Other.Y;
		Z *= Other.Z;
		W *= Other.W;

		return *this;
	}
	inline v4 &
	operator *= (f32 Value)
	{
		X *= Value;
		Y *= Value;
		Z *= Value;
		W *= Value;

		return *this;
	}
};

inline v4
MakeV4(f32 X, f32 Y, f32 Z, f32 W)
{
	v4 Result;

	Result.X = X;
	Result.Y = Y;
	Result.Z = Z;
	Result.W = W;

	return Result;
}

inline v4
operator - (v4 A)
{
	v4 Result;

	Result.X = -A.X;
	Result.Y = -A.Y;
	Result.Z = -A.Z;
	Result.W = -A.W;
	
	return Result;
}

inline v4
operator + (v4 A, v4 B)
{
	v4 Result;

	Result.X = A.X + B.X;
	Result.Y = A.Y + B.Y;
	Result.Z = A.Z + B.Z;
	Result.W = A.W + B.W;

	return Result;
}
inline v4
operator + (v4 A, f32 V)
{
	v4 Result;

	Result.X = A.X + V;
	Result.Y = A.Y + V;
	Result.Z = A.Z + V;
	Result.W = A.W + V;

	return Result;
}

inline v4
operator - (v4 A, v4 B)
{
	v4 Result;

	Result.X = A.X - B.X;
	Result.Y = A.Y - B.Y;
	Result.Z = A.Z - B.Z;
	Result.W = A.W - B.W;

	return Result;
}
inline v4
operator - (v4 A, f32 V)
{
	v4 Result;

	Result.X = A.X - V;
	Result.Y = A.Y - V;
	Result.Z = A.Z - V;
	Result.W = A.W - V;

	return Result;
}

inline v4
operator * (v4 A, v4 B)
{
	v4 Result;

	Result.X = A.X * B.X;
	Result.Y = A.Y * B.Y;
	Result.Z = A.Z * B.Z;
	Result.W = A.W * B.W;

	return Result;
}
inline v4
operator * (v4 A, f32 V)
{
	v4 Result;

	Result.X = A.X * V;
	Result.Y = A.Y * V;
	Result.Z = A.Z * V;
	Result.W = A.W * V;

	return Result;
}

// NOTE(ivan): Aliases for color.
typedef v3 rgb;
typedef v4 rgba;

inline rgb
MakeRGB(f32 R, f32 G, f32 B)
{
	rgb Result;

	Result.R = R;
	Result.G = G;
	Result.B = B;

	return Result;
}
inline rgba
MakeRGBA(f32 R, f32 G, f32 B, f32 A)
{
	rgba Result;

	Result.R = R;
	Result.G = G;
	Result.B = B;
	Result.A = A;

	return Result;
}

#endif // #ifndef ZEUS_MATH_H
