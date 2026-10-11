#include "Foundation/FoundationBlockTextureTable.h"
#include "Engine/Texture2D.h"
#include "Foundation/FoundationBlockAssets.h"
#include "Foundation/FoundationBlockRegistry.h"
#include "RenderingThread.h"
#include "TextureResource.h"

void UFoundationBlockTextureTable::Initialize(const UFoundationBlockAssets* InAssets)
{
    Assets = InAssets;
    const int32 Count = FMath::Max(1, InAssets ? InAssets->Textures.Num() : 1);
    Rows.SetNumZeroed(Count * 4);
    Animated.Reset();
    for (int32 Index = 0; InAssets && Index < InAssets->Textures.Num(); ++Index)
    {
        const FFoundationBlockTextureAsset& Entry = InAssets->Textures[Index];
        Rows[Index * 4] = Rows[Index * 4 + 1] = float(Entry.FirstLayer);
        Rows[Index * 4 + 3] = Entry.bWaterFlow ? 1.f : 0.f;
        if (Entry.IsAnimated()) Animated.Add(Index);
    }
    Texture = UTexture2D::CreateTransient(Count, 1, PF_A32B32G32R32F, TEXT("BlockTextureTable"));
    Texture->Filter = TF_Nearest;
    Texture->SRGB = false;
    Texture->NeverStream = true;
    Texture->UpdateResource();
    Tick = -1;
    SetTick(0);
}

void UFoundationBlockTextureTable::SetTick(int64 NewTick)
{
    if (NewTick == Tick || !Texture) return;
    Tick = NewTick;
    for (const int32 Index : Animated)
    {
        int32 Layer, Next;
        float Blend;
        FoundationBlockFaces::GetFrame(Assets->Textures[Index], Tick, Layer, Next, Blend);
        Rows[Index * 4] = float(Layer);
        Rows[Index * 4 + 1] = float(Next);
        Rows[Index * 4 + 2] = Blend;
    }
    Upload();
}

int32 UFoundationBlockTextureTable::GetCurrentLayer(int32 TextureIndex) const
{
    return Rows.IsValidIndex(TextureIndex * 4) ? int32(Rows[TextureIndex * 4]) : 0;
}

void UFoundationBlockTextureTable::Upload()
{
    const int32 Width = Rows.Num() / 4;
    auto* Region = new FUpdateTextureRegion2D(0, 0, 0, 0, uint32(Width), 1);
    // The render thread frees the copy and the region after uploading.
    float* Copy = static_cast<float*>(FMemory::Malloc(Rows.Num() * sizeof(float)));
    FMemory::Memcpy(Copy, Rows.GetData(), Rows.Num() * sizeof(float));
    Texture->UpdateTextureRegions(0, 1, Region, uint32(Width * 4 * sizeof(float)), 4 * sizeof(float),
        reinterpret_cast<uint8*>(Copy),
        [](uint8* Data, const FUpdateTextureRegion2D* Regions)
        {
            FMemory::Free(Data);
            delete Regions;
        });
}
