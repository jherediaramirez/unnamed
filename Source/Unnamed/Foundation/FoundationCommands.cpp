#include "Foundation/FoundationCommands.h"
#include "Clock/VoxelWorldClock.h"
#include "Foundation/FoundationAuthority.h"
#include "Foundation/FoundationChat.h"
#include "Foundation/FoundationCoordinates.h"
#include "Foundation/FoundationVitals.h"
#include "Algo/AnyOf.h"

namespace
{
    // Every string from Reference/minecraft-menu/assets/minecraft/lang/en_us.json (the 26.3 jar's), verbatim.
    const FoundationCommands::FLangString LangStrings[] = {
        {TEXT("command.unknown.command"), TEXT("Unknown or incomplete command. See below for error")},
        {TEXT("command.unknown.argument"), TEXT("Incorrect argument for command")},
        {TEXT("command.context.here"), TEXT("<--[HERE]")},
        {TEXT("command.expected.separator"), TEXT("Expected whitespace to end one argument, but found trailing data")},
        {TEXT("argument.entity.invalid"), TEXT("Invalid name or UUID")},
        {TEXT("argument.entity.selector.unknown"), TEXT("Unknown selector type '%s'")},
        {TEXT("argument.entity.selector.missing"), TEXT("Missing selector type")},
        {TEXT("argument.entity.notfound.entity"), TEXT("No entity was found")},
        {TEXT("argument.entity.notfound.player"), TEXT("No player was found")},
        {TEXT("argument.entity.toomany"),
            TEXT("Only one entity is allowed, but the provided selector allows more than one")},
        {TEXT("argument.player.entities"),
            TEXT("Only players may be affected by this command, but the provided selector includes entities")},
        {TEXT("argument.gamemode.invalid"), TEXT("Unknown game mode: %s")},
        {TEXT("argument.item.id.invalid"), TEXT("Unknown item '%s'")},
        {TEXT("argument.integer.low"), TEXT("Integer must not be less than %s: found %s")},
        {TEXT("parsing.int.expected"), TEXT("Expected integer")},
        {TEXT("parsing.int.invalid"), TEXT("Invalid integer '%s'")},
        {TEXT("parsing.double.expected"), TEXT("Expected double")},
        {TEXT("parsing.double.invalid"), TEXT("Invalid double '%s'")},
        {TEXT("parsing.float.expected"), TEXT("Expected float")},
        {TEXT("parsing.float.invalid"), TEXT("Invalid float '%s'")},
        {TEXT("argument.pos.missing.double"), TEXT("Expected a coordinate")},
        {TEXT("argument.pos3d.incomplete"), TEXT("Incomplete (expected 3 coordinates)")},
        {TEXT("argument.pos.mixed"),
            TEXT("Cannot mix world and local coordinates (everything must either use ^ or not)")},
        {TEXT("argument.time.invalid_unit"), TEXT("Invalid unit")},
        {TEXT("argument.time.tick_count_too_low"), TEXT("The tick count must not be less than %s: found %s")},
        {TEXT("gameMode.survival"), TEXT("Survival Mode")},
        {TEXT("gameMode.creative"), TEXT("Creative Mode")},
        {TEXT("gameMode.adventure"), TEXT("Adventure Mode")},
        {TEXT("gameMode.spectator"), TEXT("Spectator Mode")},
        {TEXT("commands.gamemode.success.self"), TEXT("Set own game mode to %s")},
        {TEXT("commands.give.success.single"), TEXT("Gave %s %s to %s")},
        {TEXT("commands.give.failed.toomanyitems"), TEXT("Can't give more than %s of %s")},
        {TEXT("commands.teleport.success.location.single"), TEXT("Teleported %s to %s, %s, %s")},
        {TEXT("commands.teleport.success.entity.single"), TEXT("Teleported %s to %s")},
        {TEXT("commands.teleport.invalidPosition"), TEXT("Invalid position for teleport")},
        {TEXT("commands.time.no_default_clock"), TEXT("There is no default clock in dimension %s")},
        {TEXT("commands.time.no_time_marker_found"), TEXT("Time marker %s does not exist for clock %s")},
        {TEXT("commands.time.set.absolute"), TEXT("Set %s to %s tick(s)")},
        {TEXT("commands.time.pause.already_paused"), TEXT("Clock %s is already paused")},
        {TEXT("commands.time.pause.already_running"), TEXT("Clock %s is already running")},
        {TEXT("commands.kill.success.single"), TEXT("Killed %s")},
        {TEXT("commands.kill.success.multiple"), TEXT("Killed %s entities")},
        {TEXT("chat.type.text"), TEXT("<%s> %s")},
    };

    // A parse error at a cursor (vanilla's context line follows), or an execution error (Cursor INDEX_NONE: one line).
    struct FCommandError
    {
        FString Message;
        int32 Cursor = INDEX_NONE;
    };

    FCommandError Unknown(int32 Cursor)
    {
        return {FoundationCommands::Format(TEXT("command.unknown.command")), Cursor};
    }
    FCommandError Incorrect(int32 Cursor)
    {
        return {FoundationCommands::Format(TEXT("command.unknown.argument")), Cursor};
    }

    // brigadier's StringReader, as far as these commands need it.
    struct FReader
    {
        const FString& Input;
        int32 Cursor = 0;

        bool CanRead() const { return Cursor < Input.Len(); }
        TCHAR Peek() const { return Input[Cursor]; }
        static bool IsNumber(TCHAR C) { return (C >= '0' && C <= '9') || C == '.' || C == '-'; }
        // brigadier rejects a bare sign or dot, which FString::IsNumeric accepts.
        static bool IsValidNumber(const FString& Text)
        {
            return Text.IsNumeric() && Algo::AnyOf(Text, [](TCHAR C) { return C >= '0' && C <= '9'; });
        }
        static bool IsUnquoted(TCHAR C)
        {
            return (C < 128 && FChar::IsAlnum(C)) || C == '_' || C == '-' || C == '.' || C == '+';
        }
        static bool IsResource(TCHAR C)
        {
            return (C >= '0' && C <= '9') || (C >= 'a' && C <= 'z') || C == '_' || C == ':' || C == '/' || C == '.' ||
                C == '-';
        }
        FString ReadWhile(bool (*Allowed)(TCHAR))
        {
            const int32 Start = Cursor;
            while (CanRead() && Allowed(Peek()))
                ++Cursor;
            return Input.Mid(Start, Cursor - Start);
        }
        FString ReadUntilSpace()
        {
            const int32 Start = Cursor;
            while (CanRead() && Peek() != ' ')
                ++Cursor;
            return Input.Mid(Start, Cursor - Start);
        }
        // An argument must end at a space or the end of the input.
        bool EndArgument(FCommandError& Error) const
        {
            if (CanRead() && Peek() != ' ')
            {
                Error = {FoundationCommands::Format(TEXT("command.expected.separator")), Cursor};
                return false;
            }
            return true;
        }
        // Steps over the separator before a required child; at the end, the command is incomplete.
        bool Next(FCommandError& Error)
        {
            if (!CanRead() || Cursor + 1 >= Input.Len())
            {
                Error = Unknown(Input.Len());
                return false;
            }
            ++Cursor;
            return true;
        }
        // Whether an optional child follows; anything but one space and more input is trailing data.
        bool HasMore() const { return CanRead() && Cursor + 1 < Input.Len(); }
        bool ReadInt(int32 Minimum, int32& Out, FCommandError& Error)
        {
            const int32 Start = Cursor;
            const FString Text = ReadWhile(IsNumber);
            if (Text.IsEmpty())
            {
                Error = {FoundationCommands::Format(TEXT("parsing.int.expected")), Start};
                return false;
            }
            int64 Value = 0;
            if (!IsValidNumber(Text) || Text.Contains(TEXT(".")) || !LexTryParseString(Value, *Text) ||
                Value != int32(Value))
            {
                Cursor = Start;
                Error = {FoundationCommands::Format(TEXT("parsing.int.invalid"), {Text}), Start};
                return false;
            }
            if (Value < Minimum)
            {
                Cursor = Start;
                Error = {FoundationCommands::Format(
                             TEXT("argument.integer.low"), {FString::FromInt(Minimum), FString::FromInt(int32(Value))}),
                    Start};
                return false;
            }
            Out = int32(Value);
            return EndArgument(Error);
        }
        bool ReadDouble(double& Out, FCommandError& Error)
        {
            const int32 Start = Cursor;
            const FString Text = ReadWhile(IsNumber);
            if (Text.IsEmpty())
            {
                Error = {FoundationCommands::Format(TEXT("parsing.double.expected")), Start};
                return false;
            }
            if (!IsValidNumber(Text))
            {
                Cursor = Start;
                Error = {FoundationCommands::Format(TEXT("parsing.double.invalid"), {Text}), Start};
                return false;
            }
            Out = FCString::Atod(*Text);
            return true;
        }
    };

    // An entity argument: a selector (@s @p @a @r @e @n, no [options]) or a player name.
    struct FTargetArg
    {
        TCHAR Selector = 0;
        FString Name;
    };

    bool ReadTarget(FReader& Reader, bool bPlayersOnly, FTargetArg& Out, FCommandError& Error, bool bSingle = false)
    {
        Out = FTargetArg{};
        const int32 Start = Reader.Cursor;
        if (Reader.CanRead() && Reader.Peek() == '@')
        {
            ++Reader.Cursor;
            if (!Reader.CanRead())
            {
                Error = {FoundationCommands::Format(TEXT("argument.entity.selector.missing")), Reader.Cursor};
                return false;
            }
            const int32 TypeAt = Reader.Cursor;
            const TCHAR Type = Reader.Input[Reader.Cursor++];
            if (!FCString::Strchr(TEXT("sparen"), Type))
            {
                Reader.Cursor = TypeAt;
                Error = {FoundationCommands::Format(
                             TEXT("argument.entity.selector.unknown"), {FString(TEXT("@")) + FString::Chr(Type)}),
                    TypeAt};
                return false;
            }
            if (bPlayersOnly && (Type == 'e' || Type == 'n'))
            {
                Reader.Cursor = Start;
                Error = {FoundationCommands::Format(TEXT("argument.player.entities")), Start};
                return false;
            }
            // A single-entity argument ("amount": "single"): @a and @e may match more than one (limit=1 isn't parsed).
            if (bSingle && (Type == 'a' || Type == 'e'))
            {
                Reader.Cursor = Start;
                Error = {FoundationCommands::Format(TEXT("argument.entity.toomany")), Start};
                return false;
            }
            // Selector options ([type=...]) are out of step 15's scope: the bracket is trailing data.
            Out.Selector = Type;
            return Reader.EndArgument(Error);
        }
        Out.Name = Reader.ReadUntilSpace();
        if (Out.Name.IsEmpty() || Out.Name.Len() > 16)
        {
            Reader.Cursor = Start;
            Error = {FoundationCommands::Format(TEXT("argument.entity.invalid")), Start};
            return false;
        }
        return true;
    }

    // Every selector and the player's own name (any case) find the one local player.
    bool Resolve(
        const FTargetArg& Target, const FFoundationCommandContext& Context, bool bPlayers, FCommandError& Error)
    {
        if (Target.Selector || Target.Name.Equals(Context.PlayerName, ESearchCase::IgnoreCase)) return true;
        Error = {FoundationCommands::Format(
            bPlayers ? TEXT("argument.entity.notfound.player") : TEXT("argument.entity.notfound.entity"))};
        return false;
    }

    // Vec3Argument with centre correction: ~ relative, whole absolute x and z at the block's centre.
    struct FCoordinate
    {
        bool bRelative = false;
        double Value = 0;
    };

    bool LooksLikeCoordinates(const FReader& Reader)
    {
        return Reader.CanRead() && (Reader.Peek() == '~' || Reader.Peek() == '^' || FReader::IsNumber(Reader.Peek()));
    }

    bool ReadCoordinate(FReader& Reader, bool bCenter, FCoordinate& Out, FCommandError& Error)
    {
        if (!Reader.CanRead())
        {
            Error = {FoundationCommands::Format(TEXT("argument.pos.missing.double")), Reader.Cursor};
            return false;
        }
        if (Reader.Peek() == '^')
        {
            Error = {FoundationCommands::Format(TEXT("argument.pos.mixed")), Reader.Cursor};
            return false;
        }
        if (Reader.Peek() == '~')
        {
            Out.bRelative = true;
            ++Reader.Cursor;
        }
        if (Reader.CanRead() && Reader.Peek() != ' ')
        {
            const int32 Start = Reader.Cursor;
            if (!Reader.ReadDouble(Out.Value, Error)) return false;
            if (!Out.bRelative && bCenter && !Reader.Input.Mid(Start, Reader.Cursor - Start).Contains(TEXT(".")))
                Out.Value += .5;
        }
        else if (!Out.bRelative)
        {
            Error = {FoundationCommands::Format(TEXT("argument.pos.missing.double")), Reader.Cursor};
            return false;
        }
        return true;
    }

    bool ReadPosition(FReader& Reader, FCoordinate (&Out)[3], FCommandError& Error)
    {
        const int32 Start = Reader.Cursor;
        if (Reader.CanRead() && Reader.Peek() == '^')
        {
            // Local coordinates (^) are out of step 15's scope; mixing them is vanilla's error either way.
            Error = {TEXT("Local coordinates (^ ^ ^) aren't supported yet"), Start};
            return false;
        }
        for (int32 Axis = 0; Axis < 3; ++Axis)
        {
            if (!ReadCoordinate(Reader, Axis != 1, Out[Axis], Error)) return false;
            if (Axis < 2 && (!Reader.CanRead() || Reader.Peek() != ' '))
            {
                Reader.Cursor = Start;
                Error = {FoundationCommands::Format(TEXT("argument.pos3d.incomplete")), Start};
                return false;
            }
            if (Axis < 2) ++Reader.Cursor;
        }
        return Reader.EndArgument(Error);
    }

    // TimeArgument: a number with an optional unit, t (1), s (20) or d (24,000), in whole ticks, at least Minimum
    // (26.3: 0 for /time set, -2147483648 for /time add).
    bool ReadTime(FReader& Reader, int32 Minimum, int64& OutTicks, FCommandError& Error)
    {
        const int32 Start = Reader.Cursor;
        const FString Number = Reader.ReadWhile(FReader::IsNumber);
        if (Number.IsEmpty())
        {
            Error = {FoundationCommands::Format(TEXT("parsing.float.expected")), Start};
            return false;
        }
        if (!FReader::IsValidNumber(Number))
        {
            Reader.Cursor = Start;
            Error = {FoundationCommands::Format(TEXT("parsing.float.invalid"), {Number}), Start};
            return false;
        }
        const FString Unit = Reader.ReadUntilSpace();
        const int32 Scale = Unit.IsEmpty() || Unit.Equals(TEXT("t"), ESearchCase::CaseSensitive) ? 1
            : Unit.Equals(TEXT("s"), ESearchCase::CaseSensitive)                                 ? 20
            : Unit.Equals(TEXT("d"), ESearchCase::CaseSensitive) ? FVoxelWorldClock::TicksPerDay
                                                                 : 0;
        if (!Scale)
        {
            Error = {FoundationCommands::Format(TEXT("argument.time.invalid_unit")), Reader.Cursor};
            return false;
        }
        const double Ticks = FMath::RoundToDouble(FCString::Atod(*Number) * Scale);
        if (Ticks < Minimum)
        {
            Reader.Cursor = Start;
            Error = {FoundationCommands::Format(TEXT("argument.time.tick_count_too_low"),
                         {FString::FromInt(Minimum), FString::Printf(TEXT("%.0f"), Ticks)}),
                Start};
            return false;
        }
        if (Ticks > MAX_int32)
        {
            Error = Incorrect(Start);
            return false;
        }
        OutTicks = int64(Ticks);
        return true;
    }

    FString GetModeName(EFoundationPlayerMode Mode)
    {
        static const TCHAR* Keys[] = {TEXT("gameMode.survival"), TEXT("gameMode.creative"), TEXT("gameMode.adventure"),
            TEXT("gameMode.spectator")};
        return FoundationCommands::Format(Keys[int32(Mode)]);
    }

    using FLines = TArray<TArray<FHudChatRun>>;

    void Say(FLines& Out, const FString& Text)
    {
        Out.Add({{Text, FoundationChat::White()}});
    }

    // The game mode and the lab rules that go with it, as AVoxelWorld applies a saved world's mode: creative has an
    // infinite supply, no tool wear and the creative inventory.
    void SetMode(FFoundationCommandContext& Context, EFoundationPlayerMode Mode)
    {
        Context.Vitals->Mode = Mode;
        if (!Context.Authority) return;
        const bool bCreative = Mode == EFoundationPlayerMode::Creative;
        FFoundationLabOptions& Options = Context.Authority->Options;
        Options.bInfiniteSupply = bCreative;
        Options.bToolWear = !bCreative;
        Options.bCreativeInventory = bCreative;
        if (FFoundationPlayer* Player = Context.Authority->FindPlayer(Context.PlayerId))
        {
            Player->Inventory.bInfiniteSupply = bCreative;
            ++Player->Inventory.Revision;
        }
    }

    bool RunGameMode(FReader& Reader, FFoundationCommandContext& Context, FLines& Out, FCommandError& Error)
    {
        if (!Reader.Next(Error)) return false;
        const FString Name = Reader.ReadWhile(FReader::IsUnquoted);
        static const TCHAR* Names[] = {TEXT("survival"), TEXT("creative"), TEXT("adventure"), TEXT("spectator")};
        int32 Mode = INDEX_NONE;
        for (int32 Index = 0; Index < UE_ARRAY_COUNT(Names); ++Index)
            if (Name.Equals(Names[Index], ESearchCase::CaseSensitive)) Mode = Index;
        if (Mode == INDEX_NONE)
        {
            Error = {FoundationCommands::Format(TEXT("argument.gamemode.invalid"), {Name}), Reader.Cursor};
            return false;
        }
        if (!Reader.EndArgument(Error)) return false;
        FTargetArg Target;
        Target.Selector = 's';
        if (Reader.HasMore())
        {
            ++Reader.Cursor;
            if (!ReadTarget(Reader, true, Target, Error)) return false;
        }
        if (Reader.CanRead())
        {
            Error = Incorrect(Reader.Cursor);
            return false;
        }
        if (!Resolve(Target, Context, true, Error)) return false;
        const EFoundationPlayerMode NewMode = EFoundationPlayerMode(Mode);
        if (NewMode == EFoundationPlayerMode::Adventure || NewMode == EFoundationPlayerMode::Spectator)
        {
            // Not vanilla: the project has neither mode yet (flagged in Docs/Commands.md).
            Error = {GetModeName(NewMode) + TEXT(" isn't available in this build")};
            return false;
        }
        // Vanilla's setGameMode fails silently for the mode already set.
        if (Context.Vitals->Mode == NewMode) return true;
        SetMode(Context, NewMode);
        Say(Out, FoundationCommands::Format(TEXT("commands.gamemode.success.self"), {GetModeName(NewMode)}));
        return true;
    }

    bool RunGive(FReader& Reader, FFoundationCommandContext& Context, FLines& Out, FCommandError& Error)
    {
        FTargetArg Target;
        if (!Reader.Next(Error) || !ReadTarget(Reader, true, Target, Error) || !Reader.Next(Error)) return false;
        const int32 IdStart = Reader.Cursor;
        FString Id = Reader.ReadWhile(FReader::IsResource);
        if (!Id.Contains(TEXT(":"))) Id = TEXT("minecraft:") + Id;
        const FFoundationRules* Rules = Context.Authority ? &Context.Authority->Rules : nullptr;
        FString Path = Id;
        const int32 Item = Rules && Path.RemoveFromStart(TEXT("minecraft:"), ESearchCase::CaseSensitive)
            ? Rules->FindItem(FName(*Path))
            : INDEX_NONE;
        if (Item == INDEX_NONE)
        {
            Reader.Cursor = IdStart;
            Error = {FoundationCommands::Format(TEXT("argument.item.id.invalid"), {Id}), IdStart};
            return false;
        }
        if (!Reader.EndArgument(Error)) return false;
        int32 Count = 1;
        if (Reader.HasMore())
        {
            ++Reader.Cursor;
            if (!Reader.ReadInt(1, Count, Error)) return false;
        }
        if (Reader.CanRead())
        {
            Error = Incorrect(Reader.Cursor);
            return false;
        }
        if (!Resolve(Target, Context, true, Error)) return false;
        const FFoundationItemDefinition& Definition = Rules->Items[Item];
        const int32 MaxStack = FMath::Max(1, Definition.MaxStack);
        const FString Shown = FString::Printf(TEXT("[%s]"), *Definition.DisplayName);
        if (Count > MaxStack * 100)
        {
            Error = {FoundationCommands::Format(
                TEXT("commands.give.failed.toomanyitems"), {FString::FromInt(MaxStack * 100), Shown})};
            return false;
        }
        // Whole stacks into the inventory; what does not fit drops at the player's feet, ready to pick up.
        FFoundationPlayer* Player = Context.Authority->FindPlayer(Context.PlayerId);
        for (int32 Left = Count; Left > 0;)
        {
            const int32 Stack = FMath::Min(Left, MaxStack);
            Left -= Stack;
            const int32 Added = Player ? Player->Inventory.Add(Item, Stack, MaxStack) : 0;
            if (Added < Stack)
                Context.Authority->SpawnItem(
                    Item, Stack - Added, Context.FeetCm - Context.Authority->World.OriginCm, FVector::ZeroVector, 0);
        }
        Say(Out,
            FoundationCommands::Format(
                TEXT("commands.give.success.single"), {FString::FromInt(Count), Shown, Context.PlayerName}));
        return true;
    }

    bool RunTeleport(FReader& Reader, FFoundationCommandContext& Context, FLines& Out, FCommandError& Error)
    {
        if (!Reader.Next(Error)) return false;
        FTargetArg Target, Destination;
        Target.Selector = 's';
        FCoordinate Position[3];
        bool bLocation = LooksLikeCoordinates(Reader);
        if (bLocation)
        {
            if (!ReadPosition(Reader, Position, Error)) return false;
        }
        else
        {
            // tp <destination> takes one entity; tp <targets> <location|destination> any number of targets.
            const int32 First = Reader.Cursor;
            if (!ReadTarget(Reader, false, Destination, Error, !Reader.Input.Mid(First).Contains(TEXT(" "))))
                return false;
            if (Reader.HasMore())
            {
                Target = Destination;
                ++Reader.Cursor;
                bLocation = LooksLikeCoordinates(Reader);
                if (bLocation ? !ReadPosition(Reader, Position, Error)
                              : !ReadTarget(Reader, false, Destination, Error, true))
                    return false;
            }
        }
        if (Reader.CanRead())
        {
            Error = Incorrect(Reader.Cursor);
            return false;
        }
        if (!Resolve(Target, Context, false, Error) || (!bLocation && !Resolve(Destination, Context, false, Error)))
            return false;
        if (!bLocation)
        {
            // The one player to itself: nothing moves.
            Say(Out,
                FoundationCommands::Format(
                    TEXT("commands.teleport.success.entity.single"), {Context.PlayerName, Context.PlayerName}));
            return true;
        }
        const FVector Now =
            FoundationCoordinates::ToMinecraft(Context.FeetCm, Context.MinecraftOriginCm, Context.RebaseCm);
        FVector To;
        for (int32 Axis = 0; Axis < 3; ++Axis)
            To[Axis] = (Position[Axis].bRelative ? Now[Axis] : 0) + Position[Axis].Value;
        // Level.isInSpawnableBounds: 30,000,000 blocks out, 20,000,000 up or down.
        if (FMath::Abs(To.X) > 3.e7 || FMath::Abs(To.Z) > 3.e7 || FMath::Abs(To.Y) > 2.e7)
        {
            Error = {FoundationCommands::Format(TEXT("commands.teleport.invalidPosition"))};
            return false;
        }
        if (Context.Teleport)
            Context.Teleport(FoundationCoordinates::ToUnreal(To, Context.MinecraftOriginCm, Context.RebaseCm));
        // Vanilla prints the position with %f (six decimals, Locale.ROOT).
        Say(Out,
            FoundationCommands::Format(TEXT("commands.teleport.success.location.single"),
                {Context.PlayerName, FString::Printf(TEXT("%f"), To.X), FString::Printf(TEXT("%f"), To.Y),
                    FString::Printf(TEXT("%f"), To.Z)}));
        return true;
    }

    // /time on the world clock, whose replies are 26.3's own (FVoxelWorldClock::RunCommand).
    bool RunTime(FReader& Reader, FFoundationCommandContext& Context, FLines& Out, FCommandError& Error)
    {
        const int32 VerbEnd = Reader.Cursor;
        if (!Reader.Next(Error)) return false;
        const FString Verb = Reader.ReadUntilSpace();
        TArray<FString> Args{Verb};
        const bool bAdd = Verb.Equals(TEXT("add"), ESearchCase::CaseSensitive);
        const bool bPause = Verb.Equals(TEXT("pause"), ESearchCase::CaseSensitive);
        int64 Ticks = 0;
        if (Verb.Equals(TEXT("set"), ESearchCase::CaseSensitive) || bAdd)
        {
            if (!Reader.Next(Error)) return false;
            const int32 Start = Reader.Cursor;
            int32 Marker = 0;
            const FString Word = Reader.ReadUntilSpace();
            if (Verb.Equals(TEXT("set"), ESearchCase::CaseSensitive) && !Word.IsEmpty() && !FReader::IsNumber(Word[0]))
            {
                // A time marker (day, noon, night, midnight, minecraft: optional).
                FString Path = Word;
                Path.RemoveFromStart(TEXT("minecraft:"), ESearchCase::CaseSensitive);
                // Resource ids are lower case: a marker in any other case does not exist.
                if (Context.Clock &&
                    (!Path.Equals(Path.ToLower(), ESearchCase::CaseSensitive) ||
                        !FVoxelWorldClock::FindMarker(Path, Marker)))
                {
                    Error = {FoundationCommands::Format(TEXT("commands.time.no_time_marker_found"),
                        {Word.Contains(TEXT(":")) ? Word : TEXT("minecraft:") + Word, TEXT("minecraft:overworld")})};
                    return false;
                }
                Args.Add(Path);
            }
            else
            {
                Reader.Cursor = Start;
                if (!ReadTime(Reader, bAdd ? MIN_int32 : 0, Ticks, Error)) return false;
                Args.Add(FString::Printf(TEXT("%lld"), Ticks));
            }
        }
        else if (Verb.Equals(TEXT("query"), ESearchCase::CaseSensitive))
        {
            const int32 QueryEnd = Reader.Cursor;
            if (!Reader.Next(Error)) return false;
            const FString What = Reader.ReadUntilSpace();
            if (!What.Equals(TEXT("time"), ESearchCase::CaseSensitive) &&
                !What.Equals(TEXT("day"), ESearchCase::CaseSensitive) &&
                !What.Equals(TEXT("gametime"), ESearchCase::CaseSensitive))
            {
                Error = Incorrect(QueryEnd);
                return false;
            }
            Args.Add(What);
        }
        else if (!bPause && !Verb.Equals(TEXT("resume"), ESearchCase::CaseSensitive))
        {
            Error = Incorrect(VerbEnd);
            return false;
        }
        if (Reader.CanRead())
        {
            Error = Incorrect(Reader.Cursor);
            return false;
        }
        if (!Context.Clock)
        {
            Error = {FoundationCommands::Format(TEXT("commands.time.no_default_clock"), {TEXT("minecraft:overworld")})};
            return false;
        }
        const TCHAR* const ClockId = TEXT("minecraft:overworld");
        if (bAdd)
        {
            // Here, not in the clock's own /time words, which (as voxel.Time) take no negative time.
            Context.Clock->AddTicks(Ticks);
            Say(Out,
                FoundationCommands::Format(TEXT("commands.time.set.absolute"),
                    {ClockId, FString::Printf(TEXT("%lld"), Context.Clock->GetDayClock().TotalTicks)}));
            return true;
        }
        // Pausing a paused clock (or resuming a running one) is vanilla's error.
        if (Args.Num() == 1 && Context.Clock->GetDayClock().bPaused == bPause)
        {
            Error = {FoundationCommands::Format(
                bPause ? TEXT("commands.time.pause.already_paused") : TEXT("commands.time.pause.already_running"),
                {ClockId})};
            return false;
        }
        FString Reply;
        if (!Context.Clock->RunCommand(Args, Reply))
        {
            Error = {Reply};
            return false;
        }
        Say(Out, Reply);
        return true;
    }

    bool RunKill(FReader& Reader, FFoundationCommandContext& Context, FLines& Out, FCommandError& Error)
    {
        FTargetArg Target;
        Target.Selector = 's';
        if (Reader.HasMore())
        {
            ++Reader.Cursor;
            if (!ReadTarget(Reader, false, Target, Error)) return false;
        }
        if (Reader.CanRead())
        {
            Error = Incorrect(Reader.Cursor);
            return false;
        }
        if (!Resolve(Target, Context, false, Error)) return false;
        // Entity.kill: generic_kill damage of Float.MAX_VALUE, through creative's invulnerability.
        Context.Vitals->Hurt(EFoundationDamage::Kill, TNumericLimits<float>::Max());
        // @e also kills every other entity: the item entities and experience orbs (step 11a, Codex 3 #2).
        int32 Killed = 1;
        if (Target.Selector == 'e' && Context.Authority)
            Killed += Context.Authority->RemoveItemEntities() + Context.Authority->RemoveExperienceOrbs();
        Say(Out,
            Killed == 1
                ? FoundationCommands::Format(TEXT("commands.kill.success.single"), {Context.PlayerName})
                : FoundationCommands::Format(TEXT("commands.kill.success.multiple"), {FString::FromInt(Killed)}));
        return true;
    }

    // Vanilla's context line: up to ten characters before the cursor in gray ("..." when cut), the rest in red, then
    // "<--[HERE]" in red (vanilla also underlines the rest and italicizes the marker; the HUD font has neither).
    TArray<FHudChatRun> MakeContext(const FString& Input, int32 Cursor)
    {
        Cursor = FMath::Clamp(Cursor, 0, Input.Len());
        const int32 From = FMath::Max(0, Cursor - 10);
        TArray<FHudChatRun> Runs;
        const FString Before = (Cursor > 10 ? TEXT("...") : TEXT("")) + Input.Mid(From, Cursor - From);
        if (!Before.IsEmpty()) Runs.Add({Before, FoundationChat::Gray()});
        if (Cursor < Input.Len()) Runs.Add({Input.Mid(Cursor), FoundationChat::Red()});
        Runs.Add({FoundationCommands::Format(TEXT("command.context.here")), FoundationChat::Red()});
        return Runs;
    }
}

TConstArrayView<FoundationCommands::FLangString> FoundationCommands::GetLangStrings()
{
    return LangStrings;
}

FString FoundationCommands::Format(const TCHAR* Key, TConstArrayView<FString> Args)
{
    const FLangString* Found = nullptr;
    for (const FLangString& String : LangStrings)
        if (FCString::Strcmp(String.Key, Key) == 0) Found = &String;
    check(Found);
    FString Out;
    int32 Next = 0;
    for (const TCHAR* C = Found->Text; *C; ++C)
    {
        if (C[0] == '%' && C[1] == 's')
        {
            Out += Args.IsValidIndex(Next) ? Args[Next] : FString();
            ++Next;
            ++C;
        }
        else if (C[0] == '%' && FChar::IsDigit(C[1]) && C[2] == '$' && C[3] == 's')
        {
            const int32 Index = C[1] - '1';
            Out += Args.IsValidIndex(Index) ? Args[Index] : FString();
            C += 3;
        }
        else
            Out.AppendChar(*C);
    }
    return Out;
}

TArray<TArray<FHudChatRun>> FoundationCommands::Run(const FString& Line, FFoundationCommandContext& Context)
{
    FLines Out;
    // Vanilla parses the line without its slash.
    const FString Input = Line.StartsWith(TEXT("/")) ? Line.Mid(1) : Line;
    FReader Reader{Input};
    FCommandError Error;
    const FString Name = Reader.ReadUntilSpace();
    bool bDone = false;
    // Without cheats every command needs a higher permission level: vanilla parses none of them.
    if (!Context.bCommands)
        Error = Unknown(0);
    else if (!Context.Vitals)
        Error = {TEXT("No player to run commands for")};
    else if (Name.Equals(TEXT("gamemode"), ESearchCase::CaseSensitive))
        bDone = RunGameMode(Reader, Context, Out, Error);
    else if (Name.Equals(TEXT("give"), ESearchCase::CaseSensitive))
        bDone = RunGive(Reader, Context, Out, Error);
    else if (Name.Equals(TEXT("tp"), ESearchCase::CaseSensitive) ||
        Name.Equals(TEXT("teleport"), ESearchCase::CaseSensitive))
        bDone = RunTeleport(Reader, Context, Out, Error);
    else if (Name.Equals(TEXT("time"), ESearchCase::CaseSensitive))
        bDone = RunTime(Reader, Context, Out, Error);
    else if (Name.Equals(TEXT("kill"), ESearchCase::CaseSensitive))
        bDone = RunKill(Reader, Context, Out, Error);
    else
        Error = Unknown(0);
    if (!bDone && !Error.Message.IsEmpty())
    {
        Out.Add({{Error.Message, FoundationChat::Red()}});
        if (Error.Cursor != INDEX_NONE) Out.Add(MakeContext(Input, Error.Cursor));
    }
    return Out;
}
