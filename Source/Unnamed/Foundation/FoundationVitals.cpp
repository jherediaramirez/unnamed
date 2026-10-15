#include "Foundation/FoundationVitals.h"

namespace
{
    // 26.3 damage-type data (data/minecraft/damage_type and its tags). Only mob attacks have a living
    // non-player cause, so only they scale with difficulty.
    struct FDamageRule
    {
        const TCHAR* Name;
        bool bBypassesArmor, bBypassesInvulnerability, bScalesWithDifficulty;
        float Exhaustion;
        const TCHAR* DeathMessage;
    };

    const FDamageRule& GetRule(EFoundationDamage Type)
    {
        static const FDamageRule Rules[] = {
            {TEXT("generic"), true, false, false, 0.f, TEXT("Player died")},
            {TEXT("mob"), false, false, true, .1f, TEXT("Player was slain by a mob")},
            {TEXT("fall"), true, false, false, 0.f, TEXT("Player fell from a high place")},
            {TEXT("drown"), true, false, false, 0.f, TEXT("Player drowned")},
            {TEXT("starve"), true, false, false, 0.f, TEXT("Player starved to death")},
            {TEXT("kill"), true, true, false, 0.f, TEXT("Player was killed")},
            {TEXT("player"), false, false, false, .1f, TEXT("Slain by a player")},
            {TEXT("lava"), false, false, false, .1f, TEXT("Player tried to swim in lava")},
            {TEXT("onFire"), true, false, false, 0.f, TEXT("Player burned to death")},
        };
        return Rules[int32(Type)];
    }

    // Java's Math.round on a non-negative float.
    float RoundCm(double Cm)
    {
        return float(FMath::FloorToInt(float(Cm) + .5f));
    }
}

void FFoundationVitals::Step(const FFoundationVitalsStep& Facts)
{
    // Attack recovery and age count every player tick, through the death animation too.
    ++AttackTicker;
    ++Age;
    if (IsDead())
    {
        ++DeathTicks;
        return;
    }
    if (DamageCooldown > 0) --DamageCooldown;
    bEyesInWater = Facts.bEyesInWater;

    // Movement statistics: water movement costs 0.01 per block, ground sprinting 0.1; walking is free.
    const FVector& Moved = Facts.DisplacementCm;
    if (Facts.bEyesInWater)
        AddExhaustion(.01f * RoundCm(Moved.Size()) * .01f);
    else if (Facts.bBodyInWater)
        AddExhaustion(.01f * RoundCm(Moved.Size2D()) * .01f);
    else if (Facts.bGrounded && Facts.bSprinting)
        AddExhaustion(.1f * RoundCm(Moved.Size2D()) * .01f);
    if (Facts.bJumped) AddExhaustion(Facts.bSprintJumped ? .2f : .05f);

    // Falls accumulate only downward movement outside water and land once.
    if (Facts.bBodyInWater)
        FallDistanceCm = 0;
    else if (Moved.Z < 0)
        FallDistanceCm -= Moved.Z;
    if (Facts.bGrounded)
    {
        if (const int32 Damage = GetFallDamage(FallDistanceCm)) Hurt(EFoundationDamage::Fall, float(Damage));
        FallDistanceCm = 0;
    }
    if (IsDead()) return;

    // Fire [hurt.lava_contact, hurt.lava_ignite, hurt.burning]: water puts it out; burning outside lava hurts 1
    // every 20 ticks; lava ignites for at least 300 ticks and hurts 4 every tick, which the damage cooldown turns
    // into hits at ticks 1, 11, 21 ... Creative and Spectator neither ignite nor take the damage.
    if (Facts.bBodyInWater && FireTicks > 0)
    {
        FireTicks = 0;
        ++Extinguishes;
    }
    if (FireTicks > 0)
    {
        if (FireTicks % BurnIntervalTicks == 0 && !Facts.bBodyInLava && Hurt(EFoundationDamage::OnFire, 1.f))
            ++BurnHurts;
        --FireTicks;
    }
    if (Facts.bBodyInLava && !IsDead() && !IsInvulnerable())
    {
        FireTicks = FMath::Max(FireTicks, LavaFireTicks);
        if (Hurt(EFoundationDamage::Lava, 4.f)) ++LavaHurts;
    }
    if (IsDead()) return;

    // Air: one per tick with eyes in water; drowning at -20 resets to zero. Invulnerable players
    // neither lose nor regain air underwater. Out of water it refills four per tick.
    if (Facts.bEyesInWater)
    {
        if (!IsInvulnerable() && --Air <= -20)
        {
            Air = 0;
            Hurt(EFoundationDamage::Drown, 2.f);
        }
    }
    else if (Air < MaxAir)
        Air = FMath::Min(Air + 4, MaxAir);
    if (IsDead()) return;

    // Peaceful restores a point of health every second and of food every half second.
    if (Difficulty == EFoundationDifficulty::Peaceful && bNaturalRegeneration)
    {
        if (IsHurt() && Age % 20 == 0) Heal(1.f);
        if (Food < 20 && Age % 10 == 0) ++Food;
    }
    TickFood();
}

void FFoundationVitals::TickFood()
{
    // Each four exhaustion spends one saturation, or one food once saturation is gone.
    if (Exhaustion > 4.f)
    {
        Exhaustion -= 4.f;
        if (Saturation > 0.f)
            Saturation = FMath::Max(Saturation - 1.f, 0.f);
        else
            Food = FMath::Max(Food - 1, 0);
    }
    // Saturated full food heals quickly; food >= 18 heals one point every 80 ticks; empty food starves.
    if (bNaturalRegeneration && Saturation > 0.f && IsHurt() && Food >= 20)
    {
        if (++FoodTimer >= 10)
        {
            const float Spent = FMath::Min(Saturation, 6.f);
            Heal(Spent / 6.f);
            AddFoodExhaustion(Spent);
            FoodTimer = 0;
        }
    }
    else if (bNaturalRegeneration && Food >= 18 && IsHurt())
    {
        if (++FoodTimer >= 80)
        {
            Heal(1.f);
            AddFoodExhaustion(6.f);
            FoodTimer = 0;
        }
    }
    else if (Food <= 0)
    {
        if (++FoodTimer >= 80)
        {
            // Starvation stops at 10 points on Easy and 1 on Normal; Hard is lethal.
            if (Health > 10.f || Difficulty == EFoundationDifficulty::Hard ||
                (Health > 1.f && Difficulty == EFoundationDifficulty::Normal))
                Hurt(EFoundationDamage::Starve, 1.f);
            FoodTimer = 0;
        }
    }
    else
        FoodTimer = 0;
}

bool FFoundationVitals::Hurt(EFoundationDamage Type, float Amount)
{
    const FDamageRule& Rule = GetRule(Type);
    if (IsDead() || !(Amount > 0.f) || (IsInvulnerable() && !Rule.bBypassesInvulnerability)) return false;
    float Damage = Amount;
    if (Rule.bScalesWithDifficulty)
    {
        // Mobs cannot hurt a Peaceful player.
        if (Difficulty == EFoundationDifficulty::Peaceful) return false;
        if (Difficulty == EFoundationDifficulty::Easy)
            Damage = FMath::Min(Damage / 2.f + 1.f, Damage);
        else if (Difficulty == EFoundationDifficulty::Hard)
            Damage = Damage * 3.f / 2.f;
    }
    // During the first half of the 20-tick cooldown a hit deals only what exceeds the last one.
    float Dealt = Damage;
    if (IsInHurtWindow())
    {
        if (Damage <= LastHurt) return false;
        Dealt = Damage - LastHurt;
    }
    else
        DamageCooldown = 20;
    LastHurt = Damage;

    // Armor, then the absorption buffer; only damage reaching health costs exhaustion.
    if (!Rule.bBypassesArmor) Dealt = GetDamageAfterArmor(Dealt, Armor, ArmorToughness);
    const float Absorbed = FMath::Min(Dealt, Absorption);
    Absorption -= Absorbed;
    Dealt -= Absorbed;
    if (Dealt != 0.f)
    {
        AddExhaustion(Rule.Exhaustion);
        Health = FMath::Clamp(Health - Dealt, 0.f, MaxHealth);
    }
    ++DamageEvents;
    LastDamage = FString::Printf(
        TEXT("%s %g -> %g to health, %g absorbed; health %.2f"), Rule.Name, Damage, Dealt, Absorbed, Health);
    if (Health <= 0.f)
    {
        Health = 0;
        DeathTicks = 0;
        ++Deaths;
        DeathMessage = Rule.DeathMessage;
    }
    return true;
}

void FFoundationVitals::Heal(float Amount)
{
    if (!IsDead() && Health > 0.f) Health = FMath::Clamp(Health + Amount, 0.f, MaxHealth);
}

void FFoundationVitals::Eat(int32 Nutrition, float SaturationGain)
{
    Food = FMath::Clamp(Food + Nutrition, 0, 20);
    Saturation = FMath::Clamp(Saturation + SaturationGain, 0.f, float(Food));
}

void FFoundationVitals::AddExhaustion(float Amount)
{
    if (!IsInvulnerable()) AddFoodExhaustion(Amount);
}

void FFoundationVitals::AddFoodExhaustion(float Amount)
{
    Exhaustion = FMath::Min(Exhaustion + Amount, 40.f);
}

void FFoundationVitals::AddExperience(int32 Points)
{
    Progress += float(Points) / GetExperienceForNextLevel();
    TotalExperience = int32(FMath::Clamp(int64(TotalExperience) + Points, int64(0), int64(MAX_int32)));
    while (Progress < 0.f)
    {
        const float Remaining = Progress * GetExperienceForNextLevel();
        const bool bHadLevel = Level > 0;
        AddLevels(-1);
        Progress = bHadLevel ? 1.f + Remaining / GetExperienceForNextLevel() : 0.f;
    }
    while (Progress >= 1.f)
    {
        Progress = (Progress - 1.f) * GetExperienceForNextLevel();
        AddLevels(1);
        Progress /= GetExperienceForNextLevel();
    }
}

void FFoundationVitals::AddLevels(int32 Levels)
{
    Level = int32(FMath::Clamp(int64(Level) + Levels, int64(MIN_int32), int64(MAX_int32)));
    if (Level < 0)
    {
        Level = TotalExperience = 0;
        Progress = 0;
    }
}

void FFoundationVitals::GrantAbsorption(float Amount)
{
    Absorption = FMath::Max(Absorption, Amount);
}

void FFoundationVitals::UpdateMainHand(FName Item)
{
    if (Item == MainHandItem) return;
    MainHandItem = Item;
    ResetAttackStrength();
}

void FFoundationVitals::Respawn()
{
    FFoundationVitals Fresh;
    Fresh.Mode = Mode;
    Fresh.Difficulty = Difficulty;
    Fresh.bNaturalRegeneration = bNaturalRegeneration;
    Fresh.MaxHealth = Fresh.Health = MaxHealth;
    Fresh.ArmorToughness = ArmorToughness;
    Fresh.AttackSpeed = AttackSpeed;
    Fresh.Armor = Armor;
    Fresh.MainHandItem = MainHandItem;
    Fresh.DamageEvents = DamageEvents;
    Fresh.Deaths = Deaths;
    Fresh.LavaHurts = LavaHurts;
    Fresh.BurnHurts = BurnHurts;
    Fresh.Extinguishes = Extinguishes;
    Fresh.LastDamage = LastDamage;
    Fresh.DeathMessage = DeathMessage;
    *this = MoveTemp(Fresh);
}

bool FFoundationVitals::IsInvulnerable() const
{
    return Mode == EFoundationPlayerMode::Creative || Mode == EFoundationPlayerMode::Spectator;
}

bool FFoundationVitals::HasEnoughFood() const
{
    return Food > 6 || IsInvulnerable();
}

float FFoundationVitals::GetAttackDelay() const
{
    return float(1. / FMath::Max(double(AttackSpeed), 1e-3) * 20.);
}

float FFoundationVitals::GetAttackStrength() const
{
    return FMath::Clamp(AttackTicker / GetAttackDelay(), 0.f, 1.f);
}

float FFoundationVitals::GetAttackStrength(float PartialTicks) const
{
    return FMath::Clamp((AttackTicker + PartialTicks) / GetAttackDelay(), 0.f, 1.f);
}

int32 FFoundationVitals::GetExperienceForNextLevel() const
{
    return Level >= 30 ? 112 + (Level - 30) * 9 : Level >= 15 ? 37 + (Level - 15) * 5 : 7 + Level * 2;
}

float FFoundationVitals::GetDamageAfterArmor(float Damage, int32 ArmorPoints, float Toughness)
{
    const float Effective = FMath::Clamp(ArmorPoints - Damage / (2.f + Toughness / 4.f), ArmorPoints * .2f, 20.f);
    return Damage * (1.f - Effective / 25.f);
}

int32 FFoundationVitals::GetFallDamage(double FallCm)
{
    // Three blocks are safe; the epsilon keeps an exact four-block fall at one point.
    return FMath::Max(0, FMath::FloorToInt(FallCm / 100. + 1e-6 - 3.));
}

FString FFoundationVitals::Describe() const
{
    static const TCHAR* Modes[] = {TEXT("Survival"), TEXT("Creative"), TEXT("Adventure"), TEXT("Spectator")};
    static const TCHAR* Difficulties[] = {TEXT("Peaceful"), TEXT("Easy"), TEXT("Normal"), TEXT("Hard")};
    return FString::Printf(TEXT("%s %s regen=%d | health %.2f/%.0f absorption %.1f armor %d/%.0f | food %d "
                                "saturation %.2f exhaustion %.3f | air %d/%d eyes-water=%d | XP L%d %.3f (%d) | "
                                "attack %.2f of %.1f ticks%s%s"),
        Modes[int32(Mode)], Difficulties[int32(Difficulty)], bNaturalRegeneration, Health, MaxHealth, Absorption, Armor,
        ArmorToughness, Food, Saturation, Exhaustion, Air, MaxAir, bEyesInWater, Level, Progress, TotalExperience,
        GetAttackStrength(), GetAttackDelay(), IsOnFire() ? *FString::Printf(TEXT(" | fire %d"), FireTicks) : TEXT(""),
        IsDead() ? TEXT(" | DEAD") : TEXT(""));
}
