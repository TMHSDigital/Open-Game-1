#pragma once

#include "CoreMinimal.h"

class UCSMovementTuning;

/**
 * Changes to the packaged movement tuning, made in a running build and kept in a file.
 *
 * A packaged build cannot edit DA_MovementTuning, so the values the owner changes on the tuning tab
 * are kept as a list of (row path, value) pairs and applied on top of a copy of the asset. Only the
 * paths of the range table (CSTuningRanges.h) may be named; bDevTools is not one of them.
 *
 * File shape: {"version": 1, "build": "0.1.4", "values": {"WalkSpeed": 200, "HitProfile.PositionStrength": 80}}
 */
struct CSCHARACTER_API FCSTuningOverrides
{
	static constexpr int32 CurrentVersion = 1;

	int32 Version = CurrentVersion;
	/** Project version of the build that wrote the values; empty when unknown. */
	FString Build;
	/** Row path -> value. */
	TMap<FString, float> Values;

	/**
	 * Reads the file shape above. False, with OutError naming the cause, when the text is not JSON,
	 * is not that shape, has another version, or holds a value that is not a finite number. Paths
	 * and ranges are checked by ApplyTo.
	 */
	bool Parse(const FString& Json, FString& OutError);

	/** The file shape above, with the values sorted by path. */
	FString ToJson() const;

	/**
	 * Writes every value into Target. False, with OutError naming the path, when a path is not a row
	 * of the range table or a value lies outside its row's range; Target is then left untouched.
	 * Rules between fields are not checked here: run UCSMovementTuning::IsValid on the result.
	 */
	bool ApplyTo(UCSMovementTuning& Target, FString& OutError) const;

	/** Drops every entry whose value equals the packaged one, so Values counts real changes. */
	void PruneEqualTo(const UCSMovementTuning& Packaged);

	/** The entries of Active that differ from Packaged, as overrides. */
	static FCSTuningOverrides Diff(const UCSMovementTuning& Packaged, const UCSMovementTuning& Active);
};

/** Where the overrides text is kept. The game uses a file; tests use memory. */
class CSCHARACTER_API ICSTuningOverrideStore
{
public:
	virtual ~ICSTuningOverrideStore() = default;
	/** False when nothing is stored. */
	virtual bool Load(FString& OutJson) = 0;
	/** False when the text could not be written. */
	virtual bool Save(const FString& Json) = 0;
	/** Removes what is stored. False when it could not be removed. */
	virtual bool Clear() = 0;
	/** What to call the store in a message, for example the file path. */
	virtual FString Describe() const = 0;
};

/** <ProjectSavedDir>/Tuning/MovementTuning.json. */
class CSCHARACTER_API FCSTuningOverrideFileStore : public ICSTuningOverrideStore
{
public:
	static FString GetPath();
	virtual bool Load(FString& OutJson) override;
	virtual bool Save(const FString& Json) override;
	virtual bool Clear() override;
	virtual FString Describe() const override { return GetPath(); }
};
