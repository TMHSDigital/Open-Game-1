#include "CSSocketBuilder.h"

#include "AnimationRuntime.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/SkeletalMeshSocket.h"

bool UCSSocketBuilder::SetMeshSocket(USkeletalMesh* Mesh, FName SocketName, FName BoneName, FVector MeshSpaceLocation, FRotator MeshSpaceRotation, FString& OutMessage)
{
	if (!Mesh || SocketName.IsNone())
	{
		OutMessage = TEXT("No mesh or no socket name");
		UE_LOG(LogTemp, Error, TEXT("UCSSocketBuilder: %s"), *OutMessage);
		return false;
	}
	const FReferenceSkeleton& Ref = Mesh->GetRefSkeleton();
	const int32 Bone = Ref.FindBoneIndex(BoneName);
	if (Bone == INDEX_NONE)
	{
		OutMessage = FString::Printf(TEXT("%s has no bone %s"), *Mesh->GetName(), *BoneName.ToString());
		UE_LOG(LogTemp, Error, TEXT("UCSSocketBuilder: %s"), *OutMessage);
		return false;
	}

	// Socket world = socket local * bone (component space, rest pose).
	const FTransform BoneCS = FAnimationRuntime::GetComponentSpaceTransformRefPose(Ref, Bone);
	const FTransform Local = FTransform(MeshSpaceRotation, MeshSpaceLocation).GetRelativeTransform(BoneCS);

	Mesh->Modify();
	USkeletalMeshSocket* Socket = Mesh->FindSocket(SocketName);
	if (!Socket)
	{
		Socket = NewObject<USkeletalMeshSocket>(Mesh);
		Socket->SocketName = SocketName;
		Mesh->AddSocket(Socket, false);
	}
	Socket->Modify();
	Socket->BoneName = BoneName;
	Socket->RelativeLocation = Local.GetLocation();
	Socket->RelativeRotation = Local.Rotator();
	Socket->RelativeScale = FVector::OneVector;
	Mesh->MarkPackageDirty();

	const FVector Check = (Socket->GetSocketLocalTransform() * BoneCS).GetLocation();
	OutMessage = FString::Printf(TEXT("socket %s on %s: local %s, rest pose %s (mesh space)"),
		*SocketName.ToString(), *BoneName.ToString(), *Local.GetLocation().ToString(), *Check.ToString());
	if (!Check.Equals(MeshSpaceLocation, 0.01f))
	{
		OutMessage = TEXT("socket ended up away from the requested location: ") + OutMessage;
		UE_LOG(LogTemp, Error, TEXT("UCSSocketBuilder: %s"), *OutMessage);
		return false;
	}
	return true;
}
