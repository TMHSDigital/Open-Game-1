#pragma once

#include "CoreMinimal.h"
#include "CSRunSetup.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "CSRunStore.generated.h"

/**
 * The run setup that outlives a map: the one the player is editing on the Run tab (Draft), and the
 * one waiting to start when a run was asked for on another map (the game opens that map, and the new
 * map's run director takes it).
 *
 * The draft is kept on this machine in <ProjectSavedDir>/Runs/RunSetup.json, read when the game
 * starts and written whenever it changes, so the next session opens on the last setup. A file that
 * cannot be read is left alone and the defaults are used; LastLoadNote says why.
 */
UCLASS()
class COMBATSIMULATOR_API UCSRunStore : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	static UCSRunStore* Get(const UObject* WorldContext);

	/** False: nothing is read or written (tests; the player's file is never touched by an automation run). */
	static bool bUseFile;

	static FString GetFilePath();

	const FCSRunSetup& GetDraft() const { return Draft; }

	/** Replaces the draft and saves it. */
	void SetDraft(const FCSRunSetup& Setup);

	/** Changes one field of the draft (FCSRunSetup::SetValue). bSave false while a slider is dragged. */
	bool SetDraftValue(FName Key, float Value, bool bSave);

	void SetDraftMap(const FString& Map, bool bSave);

	void SaveDraft() const;

	/** A run to start as soon as the next map is up. */
	void SetPending(const FCSRunSetup& Setup) { Pending = Setup; }
	bool HasPending() const { return Pending.IsSet(); }

	/** Takes the waiting run, if there is one. */
	bool TakePending(FCSRunSetup& OutSetup);

	/** What reading the file said: empty when it was read cleanly or there was none. */
	const FString& GetLastLoadNote() const { return LastLoadNote; }

	virtual void Initialize(FSubsystemCollectionBase& Collection) override;

private:
	FCSRunSetup Draft;
	TOptional<FCSRunSetup> Pending;
	FString LastLoadNote;
};
