#include "Foundation/FoundationBlockMipmaps.h"

// Vanilla evaluates every step in strict IEEE float/double with no fused multiply-add or reassociation. Pin that here
// so a global /fp:fast or AVX2 contraction cannot change a single bit of the output.
#if defined(__clang__)
#pragma clang fp contract(off)
#elif defined(_MSC_VER)
#pragma float_control(precise, on, push)
#pragma fp_contract(off)
#endif

namespace FoundationBlockMipmaps
{
    namespace Private
    {
        // Mirrors NativeImage: pixels are ARGB ints (alpha in the top byte), as returned by NativeImage.getPixel.
        struct FImage
        {
            int32 Width = 0;
            int32 Height = 0;
            TArray<uint32> Argb;

            uint32 Get(int32 X, int32 Y) const { return Argb[X + Y * Width]; }
            void Set(int32 X, int32 Y, uint32 Value) { Argb[X + Y * Width] = Value; }
        };

        // net.minecraft.util.ARGB channel accessors.
        inline int32 Alpha(uint32 Color)
        {
            return int32(Color >> 24);
        }
        inline int32 Red(uint32 Color)
        {
            return int32((Color >> 16) & 255u);
        }
        inline int32 Green(uint32 Color)
        {
            return int32((Color >> 8) & 255u);
        }
        inline int32 Blue(uint32 Color)
        {
            return int32(Color & 255u);
        }
        inline uint32 Color(int32 A, int32 R, int32 G, int32 B)
        {
            return (uint32(A & 255) << 24) | (uint32(R & 255) << 16) | (uint32(G & 255) << 8) | uint32(B & 255);
        }

        // Java Math.round(float): floor(x + 1/2), exact (the double addition of a float and 0.5 is exact here).
        inline int32 JavaRound(float Value)
        {
            return int32(FMath::FloorToDouble(double(Value) + 0.5));
        }

        // Mth.floor(float): (int)Math.floor((double)value).
        inline int32 JavaFloor(float Value)
        {
            return int32(FMath::FloorToDouble(double(Value)));
        }

        // Math.clamp(float, float, float) for finite inputs.
        inline float JavaClamp(float Value, float Min, float Max)
        {
            return Value < Min ? Min : (Value > Max ? Max : Value);
        }

        // ARGB.computeSrgbToLinear / computeLinearToSrgb: float in, double pow, narrowed back to float.
        inline float ComputeSrgbToLinear(float C)
        {
            return C >= 0.04045f ? float(::pow((double(C) + 0.055) / 1.055, 2.4)) : C / 12.92f;
        }

        inline float ComputeLinearToSrgb(float C)
        {
            return C >= 0.0031308f ? float(1.055 * ::pow(double(C), 0.4166666666666667) - 0.055) : 12.92f * C;
        }

        // ARGB.SRGB_TO_LINEAR (256 entries, 0..1023) and ARGB.LINEAR_TO_SRGB (1024 entries, 0..255).
        struct FChannelTables
        {
            uint16 SrgbToLinear[256];
            uint8 LinearToSrgb[1024];

            FChannelTables()
            {
                for (int32 Index = 0; Index < 256; ++Index)
                {
                    SrgbToLinear[Index] = uint16(JavaRound(ComputeSrgbToLinear(float(Index) / 255.0f) * 1023.0f));
                }
                for (int32 Index = 0; Index < 1024; ++Index)
                {
                    LinearToSrgb[Index] = uint8(JavaRound(ComputeLinearToSrgb(float(Index) / 1023.0f) * 255.0f));
                }
            }
        };

        const FChannelTables& Tables()
        {
            static const FChannelTables Instance;
            return Instance;
        }

        // ARGB.srgbToLinearChannel / linearToSrgbChannel.
        inline float SrgbToLinearChannel(int32 Channel)
        {
            return float(Tables().SrgbToLinear[Channel]) / 1023.0f;
        }

        inline int32 LinearToSrgbChannel(float Linear)
        {
            return Tables().LinearToSrgb[FMath::Clamp(JavaFloor(Linear * 1023.0f), 0, 1023)];
        }

        // ARGB.linearChannelMean: the mean is taken in 10-bit linear space with integer division.
        inline int32 LinearChannelMean(int32 A, int32 B, int32 C, int32 D)
        {
            const FChannelTables& T = Tables();
            const int32 Mean =
                (int32(T.SrgbToLinear[A]) + T.SrgbToLinear[B] + T.SrgbToLinear[C] + T.SrgbToLinear[D]) / 4;
            return T.LinearToSrgb[Mean];
        }

        // ARGB.meanLinear: alpha is a plain integer mean, colour channels are averaged in linear space.
        uint32 MeanLinear(uint32 P0, uint32 P1, uint32 P2, uint32 P3)
        {
            return Color((Alpha(P0) + Alpha(P1) + Alpha(P2) + Alpha(P3)) / 4,
                LinearChannelMean(Red(P0), Red(P1), Red(P2), Red(P3)),
                LinearChannelMean(Green(P0), Green(P1), Green(P2), Green(P3)),
                LinearChannelMean(Blue(P0), Blue(P1), Blue(P2), Blue(P3)));
        }

        // MipmapGenerator.darkenedAlphaBlend: like meanLinear, but fully transparent samples contribute nothing to the
        // sum (yet still count in the divide by 4), and alpha is averaged in linear space too. Darkens leaf edges.
        uint32 DarkenedAlphaBlend(uint32 P0, uint32 P1, uint32 P2, uint32 P3)
        {
            float A = 0.0f;
            float R = 0.0f;
            float G = 0.0f;
            float B = 0.0f;
            const uint32 Samples[4] = {P0, P1, P2, P3};
            for (const uint32 Sample : Samples)
            {
                if (Alpha(Sample) != 0)
                {
                    A += SrgbToLinearChannel(Alpha(Sample));
                    R += SrgbToLinearChannel(Red(Sample));
                    G += SrgbToLinearChannel(Green(Sample));
                    B += SrgbToLinearChannel(Blue(Sample));
                }
            }
            A /= 4.0f;
            R /= 4.0f;
            G /= 4.0f;
            B /= 4.0f;
            return Color(
                LinearToSrgbChannel(A), LinearToSrgbChannel(R), LinearToSrgbChannel(G), LinearToSrgbChannel(B));
        }

        // TextureUtil.solidify: every fully transparent pixel takes the RGB of its nearest (4-neighbour BFS) pixel with
        // alpha != 0 and keeps alpha 0, so averaging never blends in stale colour. Ties follow vanilla's queue order.
        void Solidify(FImage& Image)
        {
            const int32 Width = Image.Width;
            const int32 Height = Image.Height;
            TArray<uint32> Nearest;
            Nearest.SetNumZeroed(Width * Height);
            TArray<int32> Distance;
            Distance.Init(MAX_int32, Width * Height);
            TArray<int32> Queue; // IntArrayFIFOQueue
            Queue.Reserve(Width * Height);
            int32 Head = 0;

            for (int32 X = 0; X < Width; ++X)
            {
                for (int32 Y = 0; Y < Height; ++Y)
                {
                    const uint32 Pixel = Image.Get(X, Y);
                    if (Alpha(Pixel) != 0)
                    {
                        const int32 Packed = X + Y * Width;
                        Distance[Packed] = 0;
                        Nearest[Packed] = Pixel;
                        Queue.Add(Packed);
                    }
                }
            }

            static const int32 Directions[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
            while (Head < Queue.Num())
            {
                const int32 Current = Queue[Head++];
                const int32 X = Current % Width;
                const int32 Y = Current / Width;
                for (const int32(&Direction)[2] : Directions)
                {
                    const int32 NX = X + Direction[0];
                    const int32 NY = Y + Direction[1];
                    if (NX >= 0 && NY >= 0 && NX < Width && NY < Height)
                    {
                        const int32 Neighbour = NX + NY * Width;
                        if (Distance[Neighbour] > Distance[Current] + 1)
                        {
                            Distance[Neighbour] = Distance[Current] + 1;
                            Nearest[Neighbour] = Nearest[Current];
                            Queue.Add(Neighbour);
                        }
                    }
                }
            }

            for (int32 X = 0; X < Width; ++X)
            {
                for (int32 Y = 0; Y < Height; ++Y)
                {
                    const uint32 Pixel = Image.Get(X, Y);
                    if (Alpha(Pixel) == 0)
                    {
                        // ARGB.color(0, rgb): alpha 0 with the nearest pixel's RGB.
                        Image.Set(X, Y, Nearest[X + Y * Width] & 0x00FFFFFFu);
                    }
                }
            }
        }

        // TextureUtil.fillEmptyAreasWithDarkColor: fully transparent pixels become 3/4 of the darkest (lowest R+G+B,
        // first in column-major scan order) pixel with alpha != 0, keeping alpha 0.
        void FillEmptyAreasWithDarkColor(FImage& Image)
        {
            uint32 Darkest = 0xFFFFFFFFu; // vanilla's initial -1 when no pixel has alpha
            int32 DarkestSum = MAX_int32;
            for (int32 X = 0; X < Image.Width; ++X)
            {
                for (int32 Y = 0; Y < Image.Height; ++Y)
                {
                    const uint32 Pixel = Image.Get(X, Y);
                    if (Alpha(Pixel) != 0)
                    {
                        const int32 Sum = Red(Pixel) + Green(Pixel) + Blue(Pixel);
                        if (Sum < DarkestSum)
                        {
                            DarkestSum = Sum;
                            Darkest = Pixel;
                        }
                    }
                }
            }

            const uint32 Fill = Color(0, 3 * Red(Darkest) / 4, 3 * Green(Darkest) / 4, 3 * Blue(Darkest) / 4);
            for (int32 X = 0; X < Image.Width; ++X)
            {
                for (int32 Y = 0; Y < Image.Height; ++Y)
                {
                    if (Alpha(Image.Get(X, Y)) == 0)
                    {
                        Image.Set(X, Y, Fill);
                    }
                }
            }
        }

        // MipmapGenerator.alphaTestCoverage: fraction of area whose bilinearly interpolated alpha (times AlphaScale,
        // sampled on a 4x4 grid inside every 2x2 texel quad) exceeds AlphaRef. NaN (0/0) for images under 2x2.
        float AlphaTestCoverage(const FImage& Image, float AlphaRef, float AlphaScale)
        {
            const int32 Width = Image.Width;
            const int32 Height = Image.Height;
            float Coverage = 0.0f;
            for (int32 Y = 0; Y < Height - 1; ++Y)
            {
                for (int32 X = 0; X < Width - 1; ++X)
                {
                    const float A00 = JavaClamp(float(Alpha(Image.Get(X, Y))) / 255.0f * AlphaScale, 0.0f, 1.0f);
                    const float A10 = JavaClamp(float(Alpha(Image.Get(X + 1, Y))) / 255.0f * AlphaScale, 0.0f, 1.0f);
                    const float A01 = JavaClamp(float(Alpha(Image.Get(X, Y + 1))) / 255.0f * AlphaScale, 0.0f, 1.0f);
                    const float A11 =
                        JavaClamp(float(Alpha(Image.Get(X + 1, Y + 1))) / 255.0f * AlphaScale, 0.0f, 1.0f);
                    float Count = 0.0f;
                    for (int32 I = 0; I < 4; ++I)
                    {
                        const float FY = (float(I) + 0.5f) / 4.0f;
                        for (int32 J = 0; J < 4; ++J)
                        {
                            const float FX = (float(J) + 0.5f) / 4.0f;
                            const float Sample = A00 * (1.0f - FX) * (1.0f - FY) + A10 * FX * (1.0f - FY) +
                                A01 * (1.0f - FX) * FY + A11 * FX * FY;
                            if (Sample > AlphaRef)
                            {
                                Count = Count + 1.0f;
                            }
                        }
                    }
                    Coverage = Coverage + Count / 16.0f;
                }
            }
            return Coverage / float((Width - 1) * (Height - 1));
        }

        // MipmapGenerator.scaleAlphaToCoverage: 5-step bisection of an alpha scale in [0, 4] that makes this level's
        // coverage match the level-0 coverage, then rewrites alpha as clamp(alpha * scale + bias + 0.025).
        void ScaleAlphaToCoverage(FImage& Image, float DesiredCoverage, float AlphaRef, float AlphaCutoffBias)
        {
            float Low = 0.0f;
            float High = 4.0f;
            float Mid = 1.0f;
            float BestAlphaScale = 1.0f;
            float MinError = 3.4028235E38f;

            // Under 2x2 the coverage is NaN: every comparison is false, so vanilla breaks out and keeps scale 1.
            const bool bCoverageDefined = (Image.Width - 1) * (Image.Height - 1) > 0;
            for (int32 Step = 0; bCoverageDefined && Step < 5; ++Step)
            {
                const float Coverage = AlphaTestCoverage(Image, AlphaRef, Mid);
                const float Error = FMath::Abs(Coverage - DesiredCoverage);
                if (Error < MinError)
                {
                    MinError = Error;
                    BestAlphaScale = Mid;
                }
                if (Coverage < DesiredCoverage)
                {
                    Low = Mid;
                }
                else if (Coverage > DesiredCoverage)
                {
                    High = Mid;
                }
                else
                {
                    break;
                }
                Mid = (Low + High) * 0.5f;
            }

            for (int32 Y = 0; Y < Image.Height; ++Y)
            {
                for (int32 X = 0; X < Image.Width; ++X)
                {
                    const uint32 Pixel = Image.Get(X, Y);
                    float NewAlpha = float(Alpha(Pixel)) / 255.0f;
                    NewAlpha = NewAlpha * BestAlphaScale;
                    NewAlpha = NewAlpha + AlphaCutoffBias;
                    NewAlpha = NewAlpha + 0.025f;
                    NewAlpha = JavaClamp(NewAlpha, 0.0f, 1.0f);
                    // ARGB.color(float alpha, int rgb): as8BitChannel = floor(alpha * 255).
                    Image.Set(X, Y, (uint32(JavaFloor(NewAlpha * 255.0f)) << 24) | (Pixel & 0x00FFFFFFu));
                }
            }
        }
    }

    EStrategy ParseStrategy(const FString& Name)
    {
        if (Name.Equals(TEXT("mean"), ESearchCase::CaseSensitive))
        {
            return EStrategy::Mean;
        }
        if (Name.Equals(TEXT("cutout"), ESearchCase::CaseSensitive))
        {
            return EStrategy::Cutout;
        }
        if (Name.Equals(TEXT("strict_cutout"), ESearchCase::CaseSensitive))
        {
            return EStrategy::StrictCutout;
        }
        if (Name.Equals(TEXT("dark_cutout"), ESearchCase::CaseSensitive))
        {
            return EStrategy::DarkCutout;
        }
        return EStrategy::Auto;
    }

    TArray<TArray<uint8>> Generate(TConstArrayView<uint8> Pixels, int32 Width, int32 Height, int32 MipCount,
        EStrategy Strategy, float AlphaCutoffBias)
    {
        using namespace Private;
        TArray<TArray<uint8>> Levels;
        if (Width <= 0 || Height <= 0 || MipCount < 1 || int64(Pixels.Num()) < int64(Width) * Height * 4)
        {
            return Levels;
        }

        // Level 0 is returned exactly as given.
        Levels.Emplace();
        Levels[0].Append(Pixels.GetData(), Width * Height * 4);

        FImage Base;
        Base.Width = Width;
        Base.Height = Height;
        Base.Argb.SetNumUninitialized(Width * Height);
        bool bHasTransparent = false; // Transparency.hasTransparent(): some pixel has alpha == 0
        for (int32 Index = 0; Index < Width * Height; ++Index)
        {
            const uint8* Src = Pixels.GetData() + Index * 4;
            Base.Argb[Index] = Color(Src[3], Src[0], Src[1], Src[2]);
            bHasTransparent |= Src[3] == 0;
        }

        // AUTO: transparent (alpha 0 present) -> CUTOUT, otherwise (opaque or merely translucent) -> MEAN.
        if (Strategy == EStrategy::Auto)
        {
            Strategy = bHasTransparent ? EStrategy::Cutout : EStrategy::Mean;
        }

        // Level-0 preprocessing (vanilla does this in place; we keep the original in Levels[0] and use a copy).
        if (Strategy == EStrategy::Cutout || Strategy == EStrategy::StrictCutout)
        {
            Solidify(Base);
        }
        else if (Strategy == EStrategy::DarkCutout)
        {
            FillEmptyAreasWithDarkColor(Base);
        }

        const bool bCutoutLike =
            Strategy == EStrategy::Cutout || Strategy == EStrategy::StrictCutout || Strategy == EStrategy::DarkCutout;
        const float AlphaCutoff = Strategy == EStrategy::StrictCutout ? 0.3f : 0.5f;
        const float DesiredCoverage = bCutoutLike ? AlphaTestCoverage(Base, AlphaCutoff, 1.0f) : 0.0f;

        FImage Previous = Base;
        for (int32 Level = 1; Level < MipCount; ++Level)
        {
            FImage Next;
            Next.Width = Previous.Width >> 1;
            Next.Height = Previous.Height >> 1;
            if (Next.Width <= 0 || Next.Height <= 0)
            {
                break;
            }
            Next.Argb.SetNumUninitialized(Next.Width * Next.Height);
            for (int32 X = 0; X < Next.Width; ++X)
            {
                for (int32 Y = 0; Y < Next.Height; ++Y)
                {
                    const uint32 P0 = Previous.Get(X * 2, Y * 2);
                    const uint32 P1 = Previous.Get(X * 2 + 1, Y * 2);
                    const uint32 P2 = Previous.Get(X * 2, Y * 2 + 1);
                    const uint32 P3 = Previous.Get(X * 2 + 1, Y * 2 + 1);
                    Next.Set(X, Y,
                        Strategy == EStrategy::DarkCutout ? DarkenedAlphaBlend(P0, P1, P2, P3)
                                                          : MeanLinear(P0, P1, P2, P3));
                }
            }

            // Cutout strategies rescale this level's alpha to keep level-0 coverage; the next level derives from it.
            if (bCutoutLike)
            {
                ScaleAlphaToCoverage(Next, DesiredCoverage, AlphaCutoff, AlphaCutoffBias);
            }

            TArray<uint8>& Out = Levels.Emplace_GetRef();
            Out.SetNumUninitialized(Next.Width * Next.Height * 4);
            for (int32 Index = 0; Index < Next.Width * Next.Height; ++Index)
            {
                const uint32 Pixel = Next.Argb[Index];
                Out[Index * 4 + 0] = uint8(Red(Pixel));
                Out[Index * 4 + 1] = uint8(Green(Pixel));
                Out[Index * 4 + 2] = uint8(Blue(Pixel));
                Out[Index * 4 + 3] = uint8(Alpha(Pixel));
            }
            Previous = MoveTemp(Next);
        }
        return Levels;
    }
}

#if defined(_MSC_VER) && !defined(__clang__)
#pragma float_control(pop)
#endif
