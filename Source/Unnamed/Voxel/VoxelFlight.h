#pragma once

#include "CoreMinimal.h"

class AVoxelFlyCamera;
class AVoxelWorld;

// Phase 3's acceptance flight (-VoxelFlight=<folder>, Scripts/CaptureVoxel.ps1 -Flight). Once the first view is on
// screen, voxel.Fly's camera leaves the spawn at a fixed altitude: a hover, straight legs east at 20, 40, 80 and 160
// blocks per second, then three laps of a circle three view radii across at 80. Every frame goes to flight.csv:
// position and speed, the streaming counts, the frame's chunk work on the game thread, holes, memory and frame times.
// flight.txt sums each leg up; a few screenshots show the flight. Then the game quits.
//
// A hole is a column the player could see clearly but that is missing: closer than where the edge fog starts
// (ViewRadius - 1.5 columns from the camera's column), inside the view frustum, and never on screen. Columns still
// arriving inside the fog's fade are counted apart, as late columns: crossing a column corner moves the rings
// diagonally, so a column can come from outside the view to inside the fade within the frames its mesh takes.
class FVoxelFlight
{
public:
    // EndSeconds above 0 stops the flight early (quick looks at the fog: -VoxelFlightSeconds=).
    FVoxelFlight(const FString& InFolder, double InEndSeconds);
    // Flies one frame and records it. False once the flight has finished and its files are written.
    bool Tick(AVoxelWorld& World, float DeltaSeconds);
    // Once started, the flight flies every frame, whatever streaming is doing.
    bool IsStarted() const { return bStarted; }

private:
    struct FLeg
    {
        const TCHAR* Name;
        double BlocksPerSecond;
        double Seconds;
        bool bCircle;
        // A screenshot halfway through the leg, or null.
        const TCHAR* Shot;
    };
    struct FLegTotals
    {
        int32 Frames = 0;
        double Seconds = 0;
        double TickMsSum = 0;
        double WorstTickMs = 0;
        int32 FramesOverBudget = 0;
        int32 FramesWithHoles = 0;
        int32 MostHoles = 0;
        int32 FramesWithLate = 0;
        int32 MostLate = 0;
        int32 Applied = 0;
        int64 GeneratedBefore = 0;
        int64 GeneratedAfter = 0;
        double ProcessMbFirst = 0, ProcessMbLast = 0;
        double ColumnMbFirst = 0, ColumnMbLast = 0;
        double CacheMbLast = 0;
        int32 MostJobs = 0;
    };

    void Start(AVoxelWorld& World);
    void Finish(AVoxelWorld& World);

    FString Folder;
    TArray<FLeg> Legs;
    TArray<FLegTotals> Totals;
    TWeakObjectPtr<AVoxelFlyCamera> Camera;
    bool bStarted = false;
    double Elapsed = 0;
    double EndSeconds = 0;
    // Cruise altitude and the start of the path, in centimetres.
    FVector StartCm = FVector::ZeroVector;
    double CircleRadiusCm = 0;
    FString Rows;
    TSet<int32> ShotsTaken;
    int32 HoleFramesLogged = 0;
};
