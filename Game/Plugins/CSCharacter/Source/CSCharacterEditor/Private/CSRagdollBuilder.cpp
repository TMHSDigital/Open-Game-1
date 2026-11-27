#include "CSRagdollBuilder.h"

#include "Animation/SkeletalMeshActor.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "MeshUtilitiesCommon.h"
#include "MeshUtilitiesEngine.h"
#include "PhysicsAssetUtils.h"
#include "PhysicsEngine/BodySetup.h"
#include "PhysicsEngine/PhysicsAsset.h"
#include "PhysicsEngine/PhysicsConstraintTemplate.h"
#include "PhysicsEngine/SkeletalBodySetup.h"
#include "PreviewScene.h"
#include "UObject/Package.h"

namespace
{
	FCSRagdollBuildResult Fail(const FString& Message)
	{
		FCSRagdollBuildResult R;
		R.Message = Message;
		UE_LOG(LogTemp, Error, TEXT("CSRagdollBuilder: %s"), *Message);
		return R;
	}

	TArray<FTransform> ComponentSpaceRefPose(const FReferenceSkeleton& Ref)
	{
		const TArray<FTransform>& Local = Ref.GetRefBonePose();
		TArray<FTransform> Out;
		Out.SetNum(Local.Num());
		for (int32 i = 0; i < Local.Num(); ++i)
		{
			const int32 Parent = Ref.GetParentIndex(i);
			Out[i] = Parent == INDEX_NONE ? Local[i] : Local[i] * Out[Parent];
		}
		return Out;
	}

	// The direct child of Bone farthest from it (the bone's direction), or INDEX_NONE.
	int32 FarthestChild(const FReferenceSkeleton& Ref, const TArray<FTransform>& CS, int32 Bone)
	{
		int32 Best = INDEX_NONE;
		double BestDist = 0.0;
		for (int32 i = Bone + 1; i < Ref.GetNum(); ++i)
		{
			if (Ref.GetParentIndex(i) == Bone)
			{
				const double D = FVector::Dist(CS[i].GetLocation(), CS[Bone].GetLocation());
				if (D > BestDist)
				{
					BestDist = D;
					Best = i;
				}
			}
		}
		return BestDist > 1.0 ? Best : INDEX_NONE;
	}

	EAngularConstraintMotion MotionFor(float LimitDeg)
	{
		return LimitDeg <= 0.f ? ACM_Locked : ACM_Limited;
	}

	// Pairs of bodies that overlap in the reference pose never collide with each other (as the
	// engine's own physics asset creation does), so the ragdoll does not push itself apart.
	void DisableOverlappingPairs(UPhysicsAsset* PA, USkeletalMesh* Mesh)
	{
		FPreviewScene Scene;
		UWorld* World = Scene.GetWorld();
		ASkeletalMeshActor* Actor = World->SpawnActor<ASkeletalMeshActor>(ASkeletalMeshActor::StaticClass(), FTransform::Identity);
		USkeletalMeshComponent* Comp = Actor->GetSkeletalMeshComponent();
		Comp->SetSkeletalMesh(Mesh);
		Comp->SetPhysicsAsset(PA);
		Actor->RegisterAllComponents();

		const TArray<FBodyInstance*> Bodies = Comp->Bodies;
		for (int32 A = 0; A < Bodies.Num(); ++A)
		{
			if (!Bodies[A] || !Bodies[A]->BodySetup.IsValid())
			{
				continue;
			}
			const FTransform TM = Bodies[A]->GetUnrealWorldTransform();
			for (int32 B = A + 1; B < Bodies.Num(); ++B)
			{
				if (Bodies[B] && Bodies[B]->BodySetup.IsValid() && Bodies[A]->OverlapTestForBody(TM.GetLocation(), TM.GetRotation(), Bodies[B]))
				{
					PA->DisableCollision(A, B);
				}
			}
		}
	}
}

FCSRagdollBuildResult UCSRagdollBuilder::BuildRagdollPhysicsAsset(USkeletalMesh* Mesh, const FString& AssetPath, const TArray<FCSRagdollJoint>& Joints)
{
	if (!Mesh)
	{
		return Fail(TEXT("No skeletal mesh given."));
	}
	if (Joints.IsEmpty())
	{
		return Fail(TEXT("No joints given."));
	}
	if (!FPackageName::IsValidLongPackageName(AssetPath))
	{
		return Fail(FString::Printf(TEXT("'%s' is not a long package name like /Game/Folder/PHYS_Name."), *AssetPath));
	}

	const FReferenceSkeleton& Ref = Mesh->GetRefSkeleton();
	const TArray<FTransform> CS = ComponentSpaceRefPose(Ref);

	// Joints in bone order, so every parent body exists before its children.
	TArray<TPair<int32, FCSRagdollJoint>> Sorted;
	for (const FCSRagdollJoint& J : Joints)
	{
		const int32 BoneIndex = Ref.FindBoneIndex(J.Bone);
		if (BoneIndex == INDEX_NONE)
		{
			return Fail(FString::Printf(TEXT("Bone '%s' is not in %s."), *J.Bone.ToString(), *Mesh->GetName()));
		}
		if (Sorted.ContainsByPredicate([BoneIndex](const TPair<int32, FCSRagdollJoint>& P) { return P.Key == BoneIndex; }))
		{
			return Fail(FString::Printf(TEXT("Bone '%s' is listed twice."), *J.Bone.ToString()));
		}
		Sorted.Emplace(BoneIndex, J);
	}
	Sorted.Sort([](const TPair<int32, FCSRagdollJoint>& A, const TPair<int32, FCSRagdollJoint>& B) { return A.Key < B.Key; });

	// Create the asset, or empty the existing one so a rerun gives the same result.
	const FString AssetName = FPackageName::GetLongPackageAssetName(AssetPath);
	UPackage* Package = CreatePackage(*AssetPath);
	UPhysicsAsset* PA = LoadObject<UPhysicsAsset>(Package, *AssetName, nullptr, LOAD_NoWarn | LOAD_Quiet);
	if (!PA)
	{
		if (FindObject<UObject>(Package, *AssetName))
		{
			return Fail(FString::Printf(TEXT("%s exists and is not a physics asset."), *AssetPath));
		}
		PA = NewObject<UPhysicsAsset>(Package, *AssetName, RF_Public | RF_Standalone | RF_Transactional);
		FAssetRegistryModule::AssetCreated(PA);
	}
	else
	{
		while (PA->ConstraintSetup.Num() > 0)
		{
			FPhysicsAssetUtils::DestroyConstraint(PA, PA->ConstraintSetup.Num() - 1);
		}
		while (PA->SkeletalBodySetups.Num() > 0)
		{
			FPhysicsAssetUtils::DestroyBody(PA, PA->SkeletalBodySetups.Num() - 1);
		}
		PA->CollisionDisableTable.Empty();
	}
	PA->PreviewSkeletalMesh = Mesh;

	// Capsules, oriented to the bone, fitted to the vertices whose dominant weight is the bone or
	// one of its child bones without a body (fingers into the hand, twist bones into the limb).
	FPhysAssetCreateParams Params;
	Params.GeomType = EFG_Sphyl;
	Params.VertWeight = EVW_DominantWeight;
	Params.bAutoOrientToBone = true;
	Params.bIncludeChildBones = true;
	Params.bAlwaysUseVertices = true;
	Params.bCreateConstraints = false;
	Params.bDisableCollisionsByDefault = false;

	TArray<FBoneVertInfo> Infos;
	FMeshUtilitiesEngine::CalcBoneVertInfos(Mesh, Infos, true, 0);
	TArray<int32> PhysicalBones;
	for (const TPair<int32, FCSRagdollJoint>& P : Sorted)
	{
		PhysicalBones.Add(P.Key);
	}

	for (const TPair<int32, FCSRagdollJoint>& P : Sorted)
	{
		const int32 BodyIndex = FPhysicsAssetUtils::CreateNewBody(PA, P.Value.Bone, Params);
		UBodySetup* Body = PA->SkeletalBodySetups[BodyIndex];
		if (!FPhysicsAssetUtils::CreateCollisionFromBone(Body, Mesh, P.Key, Params, Infos[P.Key], PA, PhysicalBones))
		{
			return Fail(FString::Printf(TEXT("Could not fit a capsule to bone '%s' (no vertices weighted to it?)."), *P.Value.Bone.ToString()));
		}
	}

	FString Report;
	int32 Constraints = 0;
	for (const TPair<int32, FCSRagdollJoint>& P : Sorted)
	{
		const int32 Bone = P.Key;
		const FCSRagdollJoint& J = P.Value;

		// Nearest ancestor with a body.
		int32 ParentBodyBone = Ref.GetParentIndex(Bone);
		while (ParentBodyBone != INDEX_NONE && !PhysicalBones.Contains(ParentBodyBone))
		{
			ParentBodyBone = Ref.GetParentIndex(ParentBodyBone);
		}
		if (ParentBodyBone == INDEX_NONE)
		{
			Report += FString::Printf(TEXT("%s: root body\n"), *J.Bone.ToString());
			continue;
		}

		const FVector JointPos = CS[Bone].GetLocation();
		const int32 DirectParent = Ref.GetParentIndex(Bone);
		const FVector ParentDir = (JointPos - CS[DirectParent].GetLocation()).GetSafeNormal();
		const int32 Child = FarthestChild(Ref, CS, Bone);
		const FVector BoneDir = Child != INDEX_NONE ? (CS[Child].GetLocation() - JointPos).GetSafeNormal() : ParentDir;

		// Constraint frame in component space: X (twist) along the bone, Z the hinge axis (Swing1
		// turns about Z) or, for a ball joint, the bone's own Z made perpendicular.
		FVector FrameZ = CS[Bone].GetRotation().GetAxisZ();
		if (FMath::Abs(FVector::DotProduct(FrameZ, BoneDir)) > 0.95f)
		{
			FrameZ = CS[Bone].GetRotation().GetAxisY();
		}
		float Swing1 = J.SwingLimitDeg;
		float Swing2 = J.Swing2LimitDeg >= 0.f ? J.Swing2LimitDeg : J.SwingLimitDeg;
		float CenterDeg = 0.f;
		float RestBendDeg = 0.f;
		if (J.bHinge)
		{
			// Bending further rotates the child bone about ParentDir x BoneDir (positive = flexion).
			const FVector Axis = FVector::CrossProduct(ParentDir, BoneDir);
			RestBendDeg = FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(FVector::DotProduct(ParentDir, BoneDir), -1.f, 1.f)));
			if (Axis.Size() < 0.02f)
			{
				return Fail(FString::Printf(TEXT("Hinge '%s' is straight in the rest pose; its bend direction is unknown."), *J.Bone.ToString()));
			}
			// The anatomical hinge axis runs side to side: perpendicular to the limb and to the body's
			// forward (+Y in mesh space). A realistic leg stands slightly knock-kneed, so its rest bend
			// leans sideways and would tilt the axis about 18 degrees; the rest bend only picks the sign.
			FVector Lateral = FVector::CrossProduct(BoneDir, FVector::YAxisVector);
			if (Lateral.Size() > 0.2f)
			{
				Lateral.Normalize();
				FrameZ = FVector::DotProduct(Lateral, Axis) >= 0.f ? Lateral : -Lateral;
			}
			else
			{
				FrameZ = Axis.GetSafeNormal();
			}
			// Allowed bend from straight: [-HingeExtendDeg, HingeFlexDeg]. Relative to the rest pose
			// that is [-(Extend + Rest), Flex - Rest], centred by turning the parent frame.
			Swing1 = 0.5f * (J.HingeFlexDeg + J.HingeExtendDeg);
			CenterDeg = 0.5f * (J.HingeFlexDeg - J.HingeExtendDeg) - RestBendDeg;
		}
		FQuat FrameRot = FRotationMatrix::MakeFromXZ(BoneDir, FrameZ).ToQuat();
		FQuat ParentFrameRot = FrameRot * FQuat(FVector::ZAxisVector, FMath::DegreesToRadians(CenterDeg));
		FVector ConeDir = BoneDir;
		if (!J.bHinge && (!J.ConeDirection.IsNearlyZero() || !J.Swing1Axis.IsNearlyZero()))
		{
			// The parent frame looks along the cone centre with Z on the Swing1 axis; the child frame
			// is that frame turned the shortest way onto the rest direction, so the rest pose has no twist.
			ConeDir = J.ConeDirection.IsNearlyZero() ? BoneDir : J.ConeDirection.GetSafeNormal();
			const FVector Axis = J.Swing1Axis.IsNearlyZero() ? FrameZ : J.Swing1Axis.GetSafeNormal();
			if (FMath::Abs(FVector::DotProduct(Axis, ConeDir)) > 0.95f)
			{
				return Fail(FString::Printf(TEXT("'%s': Swing1Axis is (nearly) along ConeDirection."), *J.Bone.ToString()));
			}
			ParentFrameRot = FRotationMatrix::MakeFromXZ(ConeDir, Axis).ToQuat();
			FrameRot = FQuat::FindBetweenNormals(ConeDir, BoneDir) * ParentFrameRot;
			// The rest pose must be inside the cone, or the ragdoll snaps on its first step.
			const FQuat Rel = ParentFrameRot.Inverse() * FrameRot;
			FQuat SwingPart, TwistPart;
			Rel.ToSwingTwist(FVector::XAxisVector, SwingPart, TwistPart);
			FVector SwingAxis;
			float SwingAngle;
			SwingPart.ToAxisAndAngle(SwingAxis, SwingAngle);
			const float S1 = FMath::RadiansToDegrees(SwingAngle) * SwingAxis.Z;
			const float S2 = FMath::RadiansToDegrees(SwingAngle) * SwingAxis.Y;
			if (FMath::Square(S1 / FMath::Max(Swing1, 0.01f)) + FMath::Square(S2 / FMath::Max(Swing2, 0.01f)) > 1.f)
			{
				return Fail(FString::Printf(TEXT("'%s': the rest pose (swing %.0f, %.0f) is outside its cone (%.0f, %.0f)."), *J.Bone.ToString(), S1, S2, Swing1, Swing2));
			}
		}
		const FTransform FrameCS(FrameRot, JointPos);
		const FTransform FrameCSCentred(ParentFrameRot, JointPos);

		const int32 ConstraintIndex = FPhysicsAssetUtils::CreateNewConstraint(PA, J.Bone);
		UPhysicsConstraintTemplate* Template = PA->ConstraintSetup[ConstraintIndex];
		FConstraintInstance& CI = Template->DefaultInstance;
		CI.ConstraintBone1 = J.Bone;
		CI.ConstraintBone2 = Ref.GetBoneName(ParentBodyBone);
		CI.SetRefFrame(EConstraintFrame::Frame1, FrameCS.GetRelativeTransform(CS[Bone]));
		CI.SetRefFrame(EConstraintFrame::Frame2, FrameCSCentred.GetRelativeTransform(CS[ParentBodyBone]));
		CI.SetAngularSwing1Limit(MotionFor(Swing1), Swing1);
		CI.SetAngularSwing2Limit(MotionFor(Swing2), Swing2);
		CI.SetAngularTwistLimit(MotionFor(J.TwistLimitDeg), J.TwistLimitDeg);
		Template->SetDefaultProfile(CI);
		PA->DisableCollision(PA->FindBodyIndex(J.Bone), PA->FindBodyIndex(CI.ConstraintBone2));
		++Constraints;

		Report += J.bHinge
			? FString::Printf(TEXT("%s -> %s: hinge, rest bend %.1f, bend %.0f to %.0f from straight, off-plane %.0f, twist %.0f\n"),
				*J.Bone.ToString(), *CI.ConstraintBone2.ToString(), RestBendDeg, -J.HingeExtendDeg, J.HingeFlexDeg, Swing2, J.TwistLimitDeg)
			: FString::Printf(TEXT("%s -> %s: ball, swing %.0f x %.0f, twist %.0f, cone centre %.0f deg off the rest direction\n"),
				*J.Bone.ToString(), *CI.ConstraintBone2.ToString(), Swing1, Swing2, J.TwistLimitDeg,
				FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(FVector::DotProduct(ConeDir, BoneDir), -1.f, 1.f))));
	}

	PA->UpdateBodySetupIndexMap();
	PA->UpdateBoundsBodiesArray();
	DisableOverlappingPairs(PA, Mesh);

	Mesh->SetPhysicsAsset(PA);
	Mesh->MarkPackageDirty();
	PA->MarkPackageDirty();
	PA->RefreshPhysicsAssetChange();

	FCSRagdollBuildResult R;
	R.bSuccess = true;
	R.BodyCount = PA->SkeletalBodySetups.Num();
	R.ConstraintCount = Constraints;
	R.Message = Report;
	return R;
}
