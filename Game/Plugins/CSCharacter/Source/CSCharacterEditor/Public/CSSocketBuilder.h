#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "CSSocketBuilder.generated.h"

class USkeletalMesh;

/**
 * Adds sockets to a skeletal mesh from a script. Editor Python cannot name a socket (SocketName is
 * read-only to scripts), so import scripts call this instead.
 */
UCLASS()
class CSCHARACTEREDITOR_API UCSSocketBuilder : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * Adds the socket SocketName to Mesh on BoneName, or moves it there if the mesh already has it,
	 * so that in the rest pose it sits at MeshSpaceLocation with MeshSpaceRotation (mesh component
	 * space). Marks the mesh dirty; the caller saves it. Returns false, with OutMessage saying why,
	 * when the bone does not exist.
	 */
	UFUNCTION(BlueprintCallable, Category = "CS|Editor")
	static bool SetMeshSocket(USkeletalMesh* Mesh, FName SocketName, FName BoneName, FVector MeshSpaceLocation, FRotator MeshSpaceRotation, FString& OutMessage);
};
