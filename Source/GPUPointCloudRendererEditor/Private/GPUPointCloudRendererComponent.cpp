/*************************************************************************************************
* Written by Valentin Kraft <valentin.kraft@online.de>, http://www.valentinkraft.de, 2018
**************************************************************************************************/

#include "GPUPointCloudRendererComponent.h"
#include "CoreMinimal.h"
#include "IGPUPointCloudRenderer.h"
#include "PointCloudStreamingCore.h"
#include "ConstructorHelpers.h"


DEFINE_LOG_CATEGORY(GPUPointCloudRenderer);

#define CHECK_PCR_STATUS																\
if (!IGPUPointCloudRenderer::IsAvailable() /*|| !FPointCloudModule::IsAvailable()*/) {		\
	UE_LOG(GPUPointCloudRenderer, Error, TEXT("Point Cloud Renderer module not loaded!"));	\
	return;																				\
}																						\
if (!mPointCloudCore) {																	\
	UE_LOG(GPUPointCloudRenderer, Error, TEXT("Point Cloud Core component not found!"));	\
	return;																				\
}

//const float sqrt3 = FMath::Sqrt(3);

UGPUPointCloudRendererComponent::UGPUPointCloudRendererComponent(const FObjectInitializer& ObjectInitializer)
{
	/// Set default values
	PrimaryComponentTick.bCanEverTick = true;
	//this->GetOwner()->AutoReceiveInput = EAutoReceiveInput::Player0;

	ConstructorHelpers::FObjectFinder<UMaterial> MaterialRef(TEXT("Material'/GPUPointCloudRenderer/Streaming/DynPCMat.DynPCMat'"));
	mStreamingBaseMat = MaterialRef.Object;
	mPointCloudMaterial = UMaterialInstanceDynamic::Create(mStreamingBaseMat, this->GetOwner());

	if (mPointCloudCore)
		delete mPointCloudCore;
	mPointCloudCore = IGPUPointCloudRenderer::Get().CreateStreamingInstance(mPointCloudMaterial);
}

UGPUPointCloudRendererComponent::~UGPUPointCloudRendererComponent() {
	if (mPointCloudCore)
		delete mPointCloudCore;
}

//////////////////////
// MAIN FUNCTIONS ////
//////////////////////


void UGPUPointCloudRendererComponent::SetDynamicProperties(float cloudScaling, float falloff, float splatSize, float distanceScaling, float distanceFalloff, bool overrideColor) {
	
	mSplatFalloff = falloff;
	mCloudScaling = cloudScaling;
	mSplatSize = splatSize;
	mDistanceScaling = distanceScaling;
	mDistanceFalloff = distanceFalloff;
	mShouldOverrideColor = overrideColor;
}

void UGPUPointCloudRendererComponent::SetInputAndConvert1(TArray<FLinearColor> &pointPositions, TArray<FColor> &pointColors) {
	
	CHECK_PCR_STATUS

	if (pointPositions.Num() != pointColors.Num())
		UE_LOG(GPUPointCloudRenderer, Warning, TEXT("The number of point positions doesn't match the number of point colors."));
	if (pointPositions.Num() == 0 || pointColors.Num() == 0) {
		UE_LOG(GPUPointCloudRenderer, Error, TEXT("Empty point position and/or color data."));
		return;
	}

	CreateStreamingBaseMesh(pointPositions.Num());
	mPointCloudCore->SetInput(pointPositions, pointColors);
}

void UGPUPointCloudRendererComponent::AddSnapshot(TArray<FLinearColor> &pointPositions, TArray<uint8> &pointColors, FVector offsetTranslation, FRotator offsetRotation) {

	CHECK_PCR_STATUS

		if (pointPositions.Num() * 4 != pointColors.Num())
			UE_LOG(GPUPointCloudRenderer, Warning, TEXT("The number of point positions doesn't match the number of point colors."));
	if (pointPositions.Num() == 0 || pointColors.Num() == 0) {
		UE_LOG(GPUPointCloudRenderer, Error, TEXT("Empty point position and/or color data."));
		return;
	}

	CreateStreamingBaseMesh(PCR_MAXTEXRES * PCR_MAXTEXRES);

	// Since the point is later transformed to the local coordinate system, we have to inverse transform it beforehand
	FMatrix objMatrix = this->GetComponentToWorld().ToMatrixWithScale();
	offsetTranslation = objMatrix.InverseTransformVector(offsetTranslation);

	mPointCloudCore->AddSnapshot(pointPositions, pointColors, offsetTranslation, offsetRotation);
}

void UGPUPointCloudRendererComponent::SetInput(TArray<FLinearColor> &pointPositions, TArray<uint8> &pointColors) {
	
	CHECK_PCR_STATUS

	if (pointPositions.Num()*4 != pointColors.Num())
		UE_LOG(GPUPointCloudRenderer, Warning, TEXT("The number of point positions doesn't match the number of point colors."));
	if (pointPositions.Num() == 0 || pointColors.Num() == 0) {
		UE_LOG(GPUPointCloudRenderer, Error, TEXT("Empty point position and/or color data."));
		return;
	}

	CreateStreamingBaseMesh(pointPositions.Num());
	mPointCloudCore->SetInput(pointPositions, pointColors);
}

void UGPUPointCloudRendererComponent::SetInputAndConvert2(TArray<FVector> &pointPositions, TArray<FColor> &pointColors) {
	
	CHECK_PCR_STATUS

	if (pointPositions.Num() != pointColors.Num())
		UE_LOG(GPUPointCloudRenderer, Warning, TEXT("The number of point positions doesn't match the number of point colors."));
	if (pointPositions.Num() == 0 || pointColors.Num() == 0) {
		UE_LOG(GPUPointCloudRenderer, Error, TEXT("Empty point position and/or color data."));
		return;
	}

	CreateStreamingBaseMesh(pointPositions.Num());
	mPointCloudCore->SetInput(pointPositions, pointColors);
}

void UGPUPointCloudRendererComponent::SetExtent(FBox extent) {

	CHECK_PCR_STATUS

	mPointCloudCore->SetExtent(extent);
	mExtent = extent.ToString();
}

void UGPUPointCloudRendererComponent::SaveDataToTexture(UTextureRenderTarget2D* pointPosRT, UTextureRenderTarget2D* colorsRT) {

	CHECK_PCR_STATUS

	if (!pointPosRT || !colorsRT)
		return;
	
	mPointCloudCore->SavePointPosDataToTexture(pointPosRT);

	FTimerHandle UnusedHandle;
	GetOwner()->GetWorldTimerManager().SetTimer(UnusedHandle, this, &UGPUPointCloudRendererComponent::SaveColorDataToTextureHelper, 0.1, false);

	colorsTempRT = colorsRT;
}

void UGPUPointCloudRendererComponent::SortPointCloudForDepth() {
	
	CHECK_PCR_STATUS

	if(!mPointCloudCore->SortPointCloudData())
		UE_LOG(GPUPointCloudRenderer, Error, TEXT("Could not sort the given data. Please mind the maximum point count for sorting (%d)"), PCR_MAX_SORT_COUNT );
}

//////////////////////////
// STANDARD FUNCTIONS ////
//////////////////////////


void UGPUPointCloudRendererComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	// Update core
	if (mPointCloudCore) {
		mPointCloudCore->Update(DeltaTime);
		mPointCount = mPointCloudCore->GetPointCount();
	}

	// Update shader properties
	UpdateShaderProperties();

	UpdateCameraPositionForSorting();
}


void UGPUPointCloudRendererComponent::BeginPlay() {
	
	Super::BeginPlay();

	if(mPointCloudCore)
		mPointCloudCore->currentWorld = GetWorld();
}

////////////////////////
// HELPER FUNCTIONS ////
////////////////////////


void UGPUPointCloudRendererComponent::CreateStreamingBaseMesh(int32 pointCount)
{
	CHECK_PCR_STATUS

	//Check if update is neccessary
	//if (mBaseMesh && mBaseMesh->NumPoints == pointCount)
	//	return;
	if (pointCount == 0 || !mPointCloudCore)
		return;

	mBaseMesh = NewObject<UPointCloudMeshComponent>(this, FName("PointCloud Mesh"));

	// Create base mesh
	TArray<FCustomMeshTriangle> triangles;
	BuildTriangleStack(triangles, pointCount);
	mBaseMesh->SetCustomMeshTriangles(triangles);
	mBaseMesh->RegisterComponent();
	mBaseMesh->AttachToComponent(this, FAttachmentTransformRules::KeepRelativeTransform);
	mBaseMesh->SetMaterial(0, mStreamingBaseMat);
	mBaseMesh->SetAbsolute(false, true, true);	// Disable scaling for the mesh - the scaling vector is transferred via a shader parameter in UpdateShaderProperties()
	mBaseMesh->bNeverDistanceCull = true;
	//mBaseMesh->SetCustomBounds(mPointCloudCore->GetExtent());

	// Update material
	mPointCloudMaterial = mBaseMesh->CreateAndSetMaterialInstanceDynamic(0);
	mPointCloudCore->UpdateDynamicMaterialForStreaming(mPointCloudMaterial);
}

void UGPUPointCloudRendererComponent::BuildTriangleStack(TArray<FCustomMeshTriangle> &triangles, const int32 &pointCount)
{
	triangles.SetNumUninitialized(pointCount);

	// construct equilateral triangle with x, y, z as center and normal facing z
	float a = 1.0f;				// side lenght
	float sqrt3 = FMath::Sqrt(3);
	float r = sqrt3 / 6 * a;	// radius of inscribed circle
	//float h_minus_r = a / sqrt3; // from center to tip. height - r
	float x = 0;
	float y = 0;

	for (int i = 0; i < pointCount; i++) {

		double z = i / 10.0f;

		FCustomMeshTriangle t;
		t.Vertex2 = FVector(x - a / 2.f, y - r, z);
		t.Vertex1 = FVector(x + a / 2.f, y - r, z);
		t.Vertex0 = FVector(x, y + a / sqrt3, z);

		triangles[i] = t;
	}
}

void UGPUPointCloudRendererComponent::SaveColorDataToTextureHelper() {

	if(mPointCloudCore && colorsTempRT)
		mPointCloudCore->SaveColorDataToTexture(colorsTempRT);
}

void UGPUPointCloudRendererComponent::UpdateCameraPositionForSorting()
{
	FRotator rot;
	FVector camPos;

	// Get current camera position
	if (GEngine)
		GEngine->GetFirstLocalPlayerController(GetWorld())->PlayerCameraManager->GetCameraViewPoint(camPos, rot);

	// Transform in object space of proxy mesh
	FMatrix streamingMeshMatrix = this->GetComponentToWorld().ToMatrixWithScale();
	streamingMeshMatrix = streamingMeshMatrix.ApplyScale(mCloudScaling);
	camPos = streamingMeshMatrix.InverseTransformPosition(camPos);

	if (mPointCloudCore)
		mPointCloudCore->currentCamPos = camPos;
}


void UGPUPointCloudRendererComponent::UpdateShaderProperties()
{
	if (!mPointCloudMaterial)
		return;

	auto streamingMeshMatrix = this->GetComponentToWorld().ToMatrixWithScale();
	mPointCloudMaterial->SetVectorParameterValue("ObjTransformMatrixXAxis", streamingMeshMatrix.GetUnitAxis(EAxis::X));
	mPointCloudMaterial->SetVectorParameterValue("ObjTransformMatrixYAxis", streamingMeshMatrix.GetUnitAxis(EAxis::Y));
	mPointCloudMaterial->SetVectorParameterValue("ObjTransformMatrixZAxis", streamingMeshMatrix.GetUnitAxis(EAxis::Z));
	mPointCloudMaterial->SetVectorParameterValue("ObjScale", this->GetComponentScale() * mCloudScaling);
	mPointCloudMaterial->SetScalarParameterValue("FalloffExpo", mSplatFalloff);
	mPointCloudMaterial->SetScalarParameterValue("SplatSize", mSplatSize);
	mPointCloudMaterial->SetScalarParameterValue("DistanceScaling", mDistanceScaling);
	mPointCloudMaterial->SetScalarParameterValue("DistanceFalloff", mDistanceFalloff);
	mPointCloudMaterial->SetScalarParameterValue("ShouldOverrideColor", (int)mShouldOverrideColor);
}