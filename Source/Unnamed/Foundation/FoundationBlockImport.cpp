#include "Foundation/FoundationBlockImport.h"
#include "Engine/Texture2D.h"
#include "Engine/Texture2DArray.h"
#include "Foundation/FoundationBlockMipmaps.h"
#include "GameplayTagsManager.h"

#if WITH_EDITOR
#include "AssetRegistry/AssetRegistryModule.h"
#include "IImageWrapper.h"
#include "IImageWrapperModule.h"
#include "Misc/FileHelper.h"
#include "Modules/ModuleManager.h"
#include "UObject/Package.h"
#endif

namespace
{
    // FIPS 180-4 SHA-256; the reference records every texture layer's RGBA bytes with it.
    FString Sha256Hex(TConstArrayView<uint8> Data)
    {
        static const uint32 K[64] = {0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4,
            0xab1c5ed5, 0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
            0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da, 0x983e5152,
            0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967, 0x27b70a85, 0x2e1b2138,
            0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85, 0xa2bfe8a1, 0xa81a664b, 0xc24b8b70,
            0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070, 0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5,
            0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3, 0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa,
            0xa4506ceb, 0xbef9a3f7, 0xc67178f2};
        uint32 H[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
        TArray<uint8> Message(Data.GetData(), Data.Num());
        const uint64 Bits = uint64(Data.Num()) * 8;
        Message.Add(0x80);
        while (Message.Num() % 64 != 56)
            Message.Add(0);
        for (int32 Shift = 56; Shift >= 0; Shift -= 8)
            Message.Add(uint8(Bits >> Shift));
        auto Rotr = [](uint32 X, int32 N) { return (X >> N) | (X << (32 - N)); };
        for (int32 Chunk = 0; Chunk < Message.Num(); Chunk += 64)
        {
            uint32 W[64];
            for (int32 I = 0; I < 16; ++I)
                W[I] = uint32(Message[Chunk + I * 4]) << 24 | uint32(Message[Chunk + I * 4 + 1]) << 16 |
                    uint32(Message[Chunk + I * 4 + 2]) << 8 | uint32(Message[Chunk + I * 4 + 3]);
            for (int32 I = 16; I < 64; ++I)
            {
                const uint32 S0 = Rotr(W[I - 15], 7) ^ Rotr(W[I - 15], 18) ^ (W[I - 15] >> 3);
                const uint32 S1 = Rotr(W[I - 2], 17) ^ Rotr(W[I - 2], 19) ^ (W[I - 2] >> 10);
                W[I] = W[I - 16] + S0 + W[I - 7] + S1;
            }
            uint32 A = H[0], B = H[1], C = H[2], D = H[3], E = H[4], F = H[5], G = H[6], Hh = H[7];
            for (int32 I = 0; I < 64; ++I)
            {
                const uint32 T1 = Hh + (Rotr(E, 6) ^ Rotr(E, 11) ^ Rotr(E, 25)) + ((E & F) ^ (~E & G)) + K[I] + W[I];
                const uint32 T2 = (Rotr(A, 2) ^ Rotr(A, 13) ^ Rotr(A, 22)) + ((A & B) ^ (A & C) ^ (B & C));
                Hh = G;
                G = F;
                F = E;
                E = D + T1;
                D = C;
                C = B;
                B = A;
                A = T1 + T2;
            }
            H[0] += A;
            H[1] += B;
            H[2] += C;
            H[3] += D;
            H[4] += E;
            H[5] += F;
            H[6] += G;
            H[7] += Hh;
        }
        FString Hex;
        for (const uint32 Word : H)
            Hex += FString::Printf(TEXT("%08x"), Word);
        return Hex;
    }

#if WITH_EDITOR
    template <typename T> T* CreateAsset(const FString& PackageName)
    {
        UPackage* Package = CreatePackage(*PackageName);
        T* Asset = NewObject<T>(Package, FName(*FPackageName::GetShortName(PackageName)), RF_Public | RF_Standalone);
        FAssetRegistryModule::AssetCreated(Asset);
        Package->MarkPackageDirty();
        return Asset;
    }

    // Decoded RGBA8 PNGs by path.
    bool DecodePng(const FString& Path, TArray<uint8>& OutRGBA, int32& OutWidth, int32& OutHeight)
    {
        TArray<uint8> Bytes;
        if (!FFileHelper::LoadFileToArray(Bytes, *Path)) return false;
        IImageWrapperModule& Module = FModuleManager::LoadModuleChecked<IImageWrapperModule>(TEXT("ImageWrapper"));
        const TSharedPtr<IImageWrapper> Wrapper = Module.CreateImageWrapper(EImageFormat::PNG);
        TArray64<uint8> Raw;
        if (!Wrapper || !Wrapper->SetCompressed(Bytes.GetData(), Bytes.Num()) ||
            !Wrapper->GetRaw(ERGBFormat::RGBA, 8, Raw))
            return false;
        OutWidth = int32(Wrapper->GetWidth());
        OutHeight = int32(Wrapper->GetHeight());
        OutRGBA = TArray<uint8>(Raw.GetData(), int32(Raw.Num()));
        return true;
    }

    void ApplyPixelArtSettings(UTexture* Texture, TextureMipGenSettings MipGen, TextureGroup Group)
    {
        Texture->CompressionSettings = TC_EditorIcon; // UserInterface2D: uncompressed BGRA8.
        Texture->SRGB = true;
        Texture->Filter = TF_Nearest;
        Texture->MipGenSettings = MipGen;
        Texture->LODGroup = Group;
        Texture->NeverStream = true;
    }
#endif
}

UTexture2DArray* UFoundationBlockImport::CreateTextureArray(const FString& PackageName)
{
#if WITH_EDITOR
    return CreateAsset<UTexture2DArray>(PackageName);
#else
    return nullptr;
#endif
}

UTexture2D* UFoundationBlockImport::CreateTexture(const FString& PackageName)
{
#if WITH_EDITOR
    return CreateAsset<UTexture2D>(PackageName);
#else
    return nullptr;
#endif
}

FString UFoundationBlockImport::BuildTextureArray(UTexture2DArray* Array, const TArray<FString>& PngPaths,
    const TArray<int32>& FrameIndices, const TArray<FString>& MipStrategies, const TArray<float>& AlphaCutoffBiases,
    int32 FrameSize, int32 MipCount)
{
#if WITH_EDITOR
    const int32 Layers = PngPaths.Num();
    if (!Array || Layers == 0 || FrameIndices.Num() != Layers || MipStrategies.Num() != Layers ||
        AlphaCutoffBiases.Num() != Layers || FrameSize <= 0 || MipCount <= 0 || (FrameSize >> (MipCount - 1)) < 1)
        return TEXT("BuildTextureArray: bad arguments");
    // Mips per layer, generated from each frame's RGBA pixels.
    TArray<TArray<TArray<uint8>>> LayerMips;
    TMap<FString, TTuple<TArray<uint8>, int32, int32>> Decoded;
    for (int32 Layer = 0; Layer < Layers; ++Layer)
    {
        TArray<uint8> Frame;
        Frame.SetNumUninitialized(FrameSize * FrameSize * 4);
        // A still texture twice the frame size gives its layer from its own mip chain, one level down.
        int32 Scale = 1;
        if (PngPaths[Layer] == TEXT("white"))
            FMemory::Memset(Frame.GetData(), 255, Frame.Num());
        else
        {
            if (!Decoded.Contains(PngPaths[Layer]))
            {
                TTuple<TArray<uint8>, int32, int32> Image;
                if (!DecodePng(PngPaths[Layer], Image.Get<0>(), Image.Get<1>(), Image.Get<2>()))
                    return TEXT("cannot decode ") + PngPaths[Layer];
                Decoded.Add(PngPaths[Layer], MoveTemp(Image));
            }
            const auto& [Pixels, Width, Height] = Decoded[PngPaths[Layer]];
            Scale = Width == FrameSize * 2 && Height == Width && FrameIndices[Layer] == 0 ? 2 : 1;
            const int32 Top = FrameIndices[Layer] * FrameSize;
            if (Scale == 2)
                Frame = Pixels;
            else if (Width != FrameSize || Top + FrameSize > Height)
                return FString::Printf(
                    TEXT("%s has no %dx%d frame %d"), *PngPaths[Layer], FrameSize, FrameSize, FrameIndices[Layer]);
            else
                FMemory::Memcpy(Frame.GetData(), Pixels.GetData() + Top * Width * 4, Frame.Num());
        }
        const int32 Levels = MipCount + (Scale == 2 ? 1 : 0);
        LayerMips.Add(FoundationBlockMipmaps::Generate(Frame, FrameSize * Scale, FrameSize * Scale, Levels,
            FoundationBlockMipmaps::ParseStrategy(MipStrategies[Layer]), AlphaCutoffBiases[Layer]));
        if (LayerMips.Last().Num() != Levels) return TEXT("mip generation returned the wrong level count");
        if (Scale == 2) LayerMips.Last().RemoveAt(0);
    }
    Array->PreEditChange(nullptr);
    Array->Source.Init(FrameSize, FrameSize, Layers, MipCount, TSF_BGRA8);
    for (int32 Mip = 0; Mip < MipCount; ++Mip)
    {
        // Within a mip, slices are contiguous; source stores BGRA.
        uint8* Destination = Array->Source.LockMip(0, 0, Mip);
        const int32 Size = FMath::Max(1, FrameSize >> Mip);
        for (int32 Layer = 0; Layer < Layers; ++Layer)
        {
            const TArray<uint8>& Pixels = LayerMips[Layer][Mip];
            uint8* Slice = Destination + int64(Layer) * Size * Size * 4;
            for (int32 Pixel = 0; Pixel < Size * Size; ++Pixel)
            {
                Slice[Pixel * 4 + 0] = Pixels[Pixel * 4 + 2];
                Slice[Pixel * 4 + 1] = Pixels[Pixel * 4 + 1];
                Slice[Pixel * 4 + 2] = Pixels[Pixel * 4 + 0];
                Slice[Pixel * 4 + 3] = Pixels[Pixel * 4 + 3];
            }
        }
        Array->Source.UnlockMip(0, 0, Mip);
    }
    ApplyPixelArtSettings(Array, TMGS_LeaveExistingMips, TEXTUREGROUP_World);
    Array->AddressX = Array->AddressY = TA_Clamp;
    Array->PostEditChange();
    Array->MarkPackageDirty();
    return FString();
#else
    return TEXT("editor only");
#endif
}

bool UFoundationBlockImport::ReadLayer(UTexture2DArray* Array, int32 Layer, TArray<uint8>& OutRGBA)
{
#if WITH_EDITOR
    if (!Array || !Array->Source.IsValid() || Layer < 0 || Layer >= Array->Source.GetNumSlices()) return false;
    TArray64<uint8> Mip;
    if (!Array->Source.GetMipData(Mip, 0, 0, 0)) return false;
    const int32 Size = Array->Source.GetSizeX() * Array->Source.GetSizeY();
    OutRGBA.SetNumUninitialized(Size * 4);
    const uint8* Slice = Mip.GetData() + int64(Layer) * Size * 4;
    for (int32 Pixel = 0; Pixel < Size; ++Pixel)
    {
        OutRGBA[Pixel * 4 + 0] = Slice[Pixel * 4 + 2];
        OutRGBA[Pixel * 4 + 1] = Slice[Pixel * 4 + 1];
        OutRGBA[Pixel * 4 + 2] = Slice[Pixel * 4 + 0];
        OutRGBA[Pixel * 4 + 3] = Slice[Pixel * 4 + 3];
    }
    return true;
#else
    return false;
#endif
}

bool UFoundationBlockImport::ReadLayerMip(UTexture2DArray* Array, int32 Layer, int32 Mip, TArray<uint8>& OutRGBA)
{
#if WITH_EDITOR
    if (!Array || !Array->Source.IsValid() || Layer < 0 || Layer >= Array->Source.GetNumSlices() || Mip < 0 ||
        Mip >= Array->Source.GetNumMips())
        return false;
    TArray64<uint8> Data;
    if (!Array->Source.GetMipData(Data, 0, 0, Mip)) return false;
    const int32 Size = FMath::Max(1, Array->Source.GetSizeX() >> Mip) * FMath::Max(1, Array->Source.GetSizeY() >> Mip);
    if (Data.Num() < int64(Layer + 1) * Size * 4) return false;
    OutRGBA.SetNumUninitialized(Size * 4);
    const uint8* Slice = Data.GetData() + int64(Layer) * Size * 4;
    for (int32 Pixel = 0; Pixel < Size; ++Pixel)
    {
        OutRGBA[Pixel * 4 + 0] = Slice[Pixel * 4 + 2];
        OutRGBA[Pixel * 4 + 1] = Slice[Pixel * 4 + 1];
        OutRGBA[Pixel * 4 + 2] = Slice[Pixel * 4 + 0];
        OutRGBA[Pixel * 4 + 3] = Slice[Pixel * 4 + 3];
    }
    return true;
#else
    return false;
#endif
}

bool UFoundationBlockImport::DecodePngFile(
    const FString& Path, TArray<uint8>& OutRGBA, int32& OutWidth, int32& OutHeight)
{
#if WITH_EDITOR
    return DecodePng(Path, OutRGBA, OutWidth, OutHeight);
#else
    return false;
#endif
}

FString UFoundationBlockImport::BuildIconAtlas(
    UTexture2D* Atlas, UTexture2DArray* Array, int32 Columns, const TArray<FString>& SpritePngs)
{
#if WITH_EDITOR
    if (!Atlas || !Array || Columns <= 0 || Array->Source.GetSizeX() != 16)
        return TEXT("BuildIconAtlas: bad arguments");
    const int32 Layers = Array->Source.GetNumSlices();
    const int32 Rows = (Layers + SpritePngs.Num() + Columns - 1) / Columns;
    const int32 Width = Columns * 16, Height = Rows * 16;
    TArray<uint8> Pixels;
    Pixels.SetNumZeroed(Width * Height * 4);
    // One 16 x 16 RGBA tile into the BGRA atlas.
    auto CopyTile = [&Pixels, Columns, Width](int32 Index, const TArray<uint8>& Tile)
    {
        const int32 X0 = Index % Columns * 16, Y0 = Index / Columns * 16;
        for (int32 Y = 0; Y < 16; ++Y)
            for (int32 X = 0; X < 16; ++X)
            {
                const uint8* From = &Tile[(Y * 16 + X) * 4];
                uint8* To = &Pixels[((Y0 + Y) * Width + X0 + X) * 4];
                To[0] = From[2];
                To[1] = From[1];
                To[2] = From[0];
                To[3] = From[3];
            }
    };
    TArray<uint8> Layer;
    for (int32 Index = 0; Index < Layers; ++Index)
    {
        if (!ReadLayer(Array, Index, Layer)) return TEXT("BuildIconAtlas: unreadable layer");
        CopyTile(Index, Layer);
    }
    for (int32 Sprite = 0; Sprite < SpritePngs.Num(); ++Sprite)
    {
        int32 SpriteWidth, SpriteHeight;
        if (!DecodePng(SpritePngs[Sprite], Layer, SpriteWidth, SpriteHeight) || SpriteWidth != 16 || SpriteHeight != 16)
            return TEXT("BuildIconAtlas: not a 16x16 sprite: ") + SpritePngs[Sprite];
        CopyTile(Layers + Sprite, Layer);
    }
    Atlas->PreEditChange(nullptr);
    Atlas->Source.Init(Width, Height, 1, 1, TSF_BGRA8, Pixels.GetData());
    ApplyPixelArtSettings(Atlas, TMGS_NoMipmaps, TEXTUREGROUP_UI);
    Atlas->AddressX = Atlas->AddressY = TA_Clamp;
    Atlas->PowerOfTwoMode = ETexturePowerOfTwoSetting::None;
    Atlas->PostEditChange();
    Atlas->MarkPackageDirty();
    return FString();
#else
    return TEXT("editor only");
#endif
}

FString UFoundationBlockImport::HashLayer(UTexture2DArray* Array, int32 Layer)
{
    TArray<uint8> Pixels;
    return ReadLayer(Array, Layer, Pixels) ? Sha256Hex(Pixels) : FString();
}

FColor UFoundationBlockImport::AverageLayerColor(UTexture2DArray* Array, int32 Layer, FColor Tint)
{
    TArray<uint8> Pixels;
    if (!ReadLayer(Array, Layer, Pixels)) return FColor::White;
    double Sum[3] = {0, 0, 0};
    int32 Count = 0;
    for (int32 Pixel = 0; Pixel < Pixels.Num() / 4; ++Pixel)
        if (Pixels[Pixel * 4 + 3] > 0)
        {
            for (int32 Channel = 0; Channel < 3; ++Channel)
                Sum[Channel] += Pixels[Pixel * 4 + Channel];
            ++Count;
        }
    if (Count == 0) return FColor::White;
    const bool bTinted = Tint.A > 0;
    auto Channel = [&](int32 Index, uint8 TintValue)
    { return uint8(FMath::RoundToInt(Sum[Index] / Count * (bTinted ? TintValue / 255. : 1.))); };
    return FColor(Channel(0, Tint.R), Channel(1, Tint.G), Channel(2, Tint.B), 255);
}

FGameplayTagContainer UFoundationBlockImport::MakeTags(const TArray<FString>& Names, FString& OutError)
{
    FGameplayTagContainer Tags;
    for (const FString& Name : Names)
    {
        const FGameplayTag Tag = UGameplayTagsManager::Get().RequestGameplayTag(FName(*Name), false);
        if (!Tag.IsValid())
        {
            OutError = TEXT("unregistered gameplay tag ") + Name;
            continue;
        }
        Tags.AddTag(Tag);
    }
    return Tags;
}
