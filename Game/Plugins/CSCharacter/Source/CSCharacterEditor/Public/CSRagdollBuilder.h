#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "CSRagdollBuilder.generated.h"

class USkeletalMesh;

/**
 * One ragdoll body: a capsule fitted to the vertices weighted to Bone (and to its child bones
 * that have no body of their own), jointed to the nearest ancestor bone that has a body.
 *
 * Constraint frames are built so twist runs along the bone (from the joint toward its farthest
 * child). A ball joint allows SwingLimitDeg in every direction and TwistLimitDeg of twist; with
 * ConeDirection set the cone is centred there instead of on the rest direction, and with
 * Swing1Axis and Swing2LimitDeg set it is an ellipse (human hips and shoulders: much more room to
 * the front than to the back, overhead reachable). A hinge
 * finds its bend axis from the rest pose (the plane of the parent and child bones, so the rest
 * pose needs a slight bend), allows HingeExtendDeg past straight and HingeFlexDeg of bending, and
 * SwingLimitDeg off the hinge plane.
 */
USTRUCT(BlueprintType)
struct FCSRagdollJoint
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ragdoll")
	FName Bone;

	/** Ball joint: swing in every direction. Hinge: swing off the hinge plane. Degrees, 0 locks it. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ragdoll")
	float SwingLimitDeg = 30.f;

	/** Twist about the bone, degrees either way. 0 locks it. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ragdoll")
	float TwistLimitDeg = 20.f;

	/**
	 * Ball joint: Swing2 limit (about the axis at right angles to the cone centre and Swing1Axis),
	 * degrees; negative uses SwingLimitDeg (a round cone).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ragdoll")
	float Swing2LimitDeg = -1.f;

	/**
	 * Ball joint: the bone direction at the centre of the cone, in the mesh's reference pose space
	 * (component space). Zero centres the cone on the rest direction.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ragdoll")
	FVector ConeDirection = FVector::ZeroVector;

	/**
	 * Ball joint: the axis SwingLimitDeg (Swing1) turns the bone about, in component space; made
	 * perpendicular to the cone centre. Zero uses the bone's own Z axis.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ragdoll")
	FVector Swing1Axis = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ragdoll")
	bool bHinge = false;

	/** Hinge only: bend past straight, degrees. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ragdoll")
	float HingeFlexDeg = 140.f;

	/** Hinge only: extension past straight, degrees. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ragdoll")
	float HingeExtendDeg = 5.f;
};

USTRUCT(BlueprintType)
struct FCSRagdollBuildResult
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Ragdoll")
	bool bSuccess = false;

	UPROPERTY(BlueprintReadOnly, Category = "Ragdoll")
	int32 BodyCount = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Ragdoll")
	int32 ConstraintCount = 0;

	/** The error on failure; on success a line per joint with its limits. */
	UPROPERTY(BlueprintReadOnly, Category = "Ragdoll")
	FString Message;
};

/** Builds a ragdoll physics asset from an explicit list of bodies and joint limits. */
UCLASS()
class CSCHARACTEREDITOR_API UCSRagdollBuilder : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * Creates the physics asset at AssetPath (a long package name, for example
	 * /Game/Characters/Hero/PHYS_Hero), or empties and rebuilds it if it exists, and assigns it to
	 * Mesh. Does not save; the caller saves the asset and the mesh. Rerunnable.
	 */
	UFUNCTION(BlueprintCallable, Category = "CS|Ragdoll")
	static FCSRagdollBuildResult BuildRagdollPhysicsAsset(USkeletalMesh* Mesh, const FString& AssetPath, const TArray<FCSRagdollJoint>& Joints);
};
