public static const float M_PI              = 3.1415926535897F;         // PI
public static const float M_2_PI            = 6.2831853071795F;         // 2*PI
public static const float M_PI_2            = 1.5707963267948F;         // PI/2
public static const float M_PI_4            = 0.7853981633974F;         // PI/4
public static const float M_1_OVER_PI       = 0.3183098861837F;         // 1/PI
public static const float M_2_OVER_PI       = 0.6366197723675F;         // 2/PI
public static const float INV_M_PI          = 0.31830988618379067153f;  /* 1/pi */
public static const float SQRT_M_PI         = 1.77245385090551602729f;  /* sqrt(pi) */
public static const float SQRT_2            = 1.41421356237309504880f;  /* sqrt(2) */
public static const float INV_SQRT_M_PI     = 0.56418958354775628694f;  /* 1/sqrt(pi) */
public static const float INV_2_SQRT_M_PI   = 0.28209479177387814347f;  /* 0.5/sqrt(pi) */
public static const float INV_SQRT_2_M_PI   = 0.3989422804014326779f;   /* 1/sqrt(2*pi) */
public static const float INV_SQRT_2        = 0.7071067811865475244f;   /* 1/sqrt(2) */

public static const float FLT_MAX           = 3.402823466e+38F; // max value
public static const uint UINT_MAX           = 4294967295;
public static const uint MAX_DEPTH          = 1000000;

public static const float d = 20.0f;
public static const float GHG = exp(-(0.0990567f / (d - 1.67154)));
public static const float GD = exp(-(2.20679f / (d + 3.91029)) - 0.428934);
public static float ALPHA_GD = exp(3.62489f - (8.29288f / (d + 5.52825)));
public static float W_D = exp(-(0.599085f / (d - 0.641583)) - 0.665888);