#include "skinnedMesh.h"

class BONE_HIERARCHY: public ID3DXAllocateHierarchy
{
	public:
		STDMETHOD(CreateFrame)(THIS_ LPCSTR Name, LPD3DXFRAME *ppNewFrame);
		STDMETHOD(CreateMeshContainer)(THIS_ LPCTSTR Name, CONST D3DXMESHDATA * pMeshData, CONST D3DXMATERIAL * pMaterials, CONST D3DXEFFECTINSTANCE * pEffectInstances, DWORD NumMaterials, CONST DWORD * pAdjacency, LPD3DXSKININFO pSkinInfo, LPD3DXMESHCONTAINER * ppNewMeshContainer);
		STDMETHOD(DestroyFrame)(THIS_ LPD3DXFRAME pFrameToFree);
		STDMETHOD(DestroyMeshContainer)(THIS_ LPD3DXMESHCONTAINER pMeshContainerBase);
};

HRESULT BONE_HIERARCHY::CreateFrame(LPCSTR Name, LPD3DXFRAME *ppNewFrame)
{
	BONE *newBone = new BONE;
	memset(newBone, 0, sizeof(BONE));

	//Copy name
	if(Name != NULL)
	{
		newBone->Name = new char[strlen(Name)+1];
		strcpy(newBone->Name, Name);
	}

	//Set the transformation matrices
	D3DXMatrixIdentity(&newBone->TransformationMatrix);
	D3DXMatrixIdentity(&newBone->CombinedTransformationMatrix);

	//Return the new bone...
	*ppNewFrame = (D3DXFRAME*)newBone;

	return S_OK;
}

HRESULT BONE_HIERARCHY::CreateMeshContainer(LPCSTR Name,
											CONST D3DXMESHDATA *pMeshData,
											CONST D3DXMATERIAL *pMaterials,
											CONST D3DXEFFECTINSTANCE *pEffectInstances,
											DWORD NumMaterials,
											CONST DWORD *pAdjacency,
											LPD3DXSKININFO pSkinInfo,
											LPD3DXMESHCONTAINER *ppNewMeshContainer)
{
	//Create new Bone Mesh
	BONEMESH *boneMesh = new BONEMESH;
	memset(boneMesh, 0, sizeof(BONEMESH));

	//Get mesh data
	boneMesh->OriginalMesh = pMeshData->pMesh;
	boneMesh->MeshData.pMesh = pMeshData->pMesh;
	boneMesh->MeshData.Type = pMeshData->Type;
	pMeshData->pMesh->AddRef();		//Add Reference so that the mesh isnt deallocated
	IDirect3DDevice9 *m_pDevice = NULL;	
	pMeshData->pMesh->GetDevice(&m_pDevice);	//Get m_pDevice ptr from mesh

	//Copy materials and load textures (just like with a static mesh)
	for(int i=0;i<NumMaterials;i++)
	{
		D3DXMATERIAL mtrl;
		memcpy(&mtrl, &pMaterials[i], sizeof(D3DXMATERIAL));
		boneMesh->materials.push_back(mtrl.MatD3D);

		char textureFname[200];
		strcpy(textureFname, "mesh/");
		strcat(textureFname, mtrl.pTextureFilename);

		//Load texture
		IDirect3DTexture9* newTexture = NULL;
		D3DXCreateTextureFromFile(m_pDevice, textureFname, &newTexture);
		boneMesh->textures.push_back(newTexture);
	}

	if(pSkinInfo != NULL)
	{
		//Get Skin Info
		boneMesh->pSkinInfo = pSkinInfo;
		pSkinInfo->AddRef();	//Add reference so that the SkinInfo isnt deallocated

		//Clone mesh and store in boneMesh->MeshData.pMesh
		pMeshData->pMesh->CloneMeshFVF(D3DXMESH_MANAGED, pMeshData->pMesh->GetFVF(), 
									   m_pDevice, &boneMesh->MeshData.pMesh);
		
		//Get Attribute Table
		boneMesh->MeshData.pMesh->GetAttributeTable(NULL, &boneMesh->NumAttributeGroups);
		boneMesh->attributeTable = new D3DXATTRIBUTERANGE[boneMesh->NumAttributeGroups];
		boneMesh->MeshData.pMesh->GetAttributeTable(boneMesh->attributeTable, NULL);

		//Create bone offset and current matrices
		int NumBones = pSkinInfo->GetNumBones();
		boneMesh->boneOffsetMatrices = new D3DXMATRIX[NumBones];		
		boneMesh->currentBoneMatrices = new D3DXMATRIX[NumBones];

		//Get bone offset matrices
		for(int i=0;i < NumBones;i++)
			boneMesh->boneOffsetMatrices[i] = *(boneMesh->pSkinInfo->GetBoneOffsetMatrix(i));
	}

	//Set ppNewMeshContainer to the newly created boneMesh container
	*ppNewMeshContainer = boneMesh;

	return S_OK;
}

HRESULT BONE_HIERARCHY::DestroyFrame(LPD3DXFRAME pFrameToFree) 
{
	if(pFrameToFree)
	{
		//Free name
		if(pFrameToFree->Name != NULL)
			delete [] pFrameToFree->Name;

		//Free bone
		delete pFrameToFree;
	}
	pFrameToFree = NULL;

    return S_OK; 
}

HRESULT BONE_HIERARCHY::DestroyMeshContainer(LPD3DXMESHCONTAINER pMeshContainerBase)
{
	BONEMESH *boneMesh = (BONEMESH*)pMeshContainerBase;

	//Release textures
	for(int i=0;i < boneMesh->textures.size();i++)
		if(boneMesh->textures[i] != NULL)
			boneMesh->textures[i]->Release();

	//Release mesh data
	if(boneMesh->MeshData.pMesh)boneMesh->MeshData.pMesh->Release();
	if(boneMesh->pSkinInfo)boneMesh->pSkinInfo->Release();
	if(boneMesh->OriginalMesh)boneMesh->OriginalMesh->Release();
	delete boneMesh;

    return S_OK;
}


//////////////////////////////////////////////////////////////////////////////////////////////////
//									SKINNED MESH												//
//////////////////////////////////////////////////////////////////////////////////////////////////

struct VERTEX{
	VERTEX();
	VERTEX(D3DXVECTOR3 pos, D3DCOLOR col){position = pos; color = col;}
	D3DXVECTOR3 position;
	D3DCOLOR color;
	static const DWORD FVF;
};

const DWORD VERTEX::FVF = D3DFVF_XYZ | D3DFVF_DIFFUSE;

SKINNEDMESH::SKINNEDMESH()
{
	m_pRootBone = NULL;
	m_pAnimControl = NULL;
}

SKINNEDMESH::~SKINNEDMESH()
{
	BONE_HIERARCHY boneHierarchy;
	boneHierarchy.DestroyFrame(m_pRootBone);
	if(m_pAnimControl)m_pAnimControl->Release();
}

void SKINNEDMESH::Load(char fileName[], IDirect3DDevice9 *Dev)
{
	m_pDevice = Dev;
	BONE_HIERARCHY boneHierarchy;

	D3DXLoadMeshHierarchyFromX(fileName, D3DXMESH_MANAGED, 
							   m_pDevice, &boneHierarchy,
							   NULL, &m_pRootBone, &m_pAnimControl);

	SetupBoneMatrixPointers((BONE*)m_pRootBone);

	//Update all the bones
	D3DXMATRIX i;
	D3DXMatrixIdentity(&i);
	UpdateMatrices((BONE*)m_pRootBone, &i);

	//Create Sphere
	D3DXCreateSphere(m_pDevice, 0.07f, 10, 10, &m_pSphereMesh, NULL);
}

void SKINNEDMESH::UpdateMatrices(BONE* bone, D3DXMATRIX *parentMatrix)
{
	if(bone == NULL)return;

	D3DXMatrixMultiply(&bone->CombinedTransformationMatrix,
					   &bone->TransformationMatrix,
					   parentMatrix);

	if(bone->pFrameSibling)UpdateMatrices((BONE*)bone->pFrameSibling, parentMatrix);
	if(bone->pFrameFirstChild)UpdateMatrices((BONE*)bone->pFrameFirstChild, &bone->CombinedTransformationMatrix);
}

void SKINNEDMESH::Render(BONE *bone)
{
	if(bone == NULL)bone = (BONE*)m_pRootBone;

	//If there is a mesh to render...
	if(bone->pMeshContainer != NULL)
	{
		BONEMESH *boneMesh = (BONEMESH*)bone->pMeshContainer;

		if (boneMesh->pSkinInfo != NULL)
		{		
			// set up bone transforms
			int numBones = boneMesh->pSkinInfo->GetNumBones();
			for(int i=0;i < numBones;i++)
				D3DXMatrixMultiply(&boneMesh->currentBoneMatrices[i],
								   &boneMesh->boneOffsetMatrices[i], 
								   boneMesh->boneMatrixPtrs[i]);

			//Update the skinned mesh
			BYTE *src = NULL, *dest = NULL;
			boneMesh->OriginalMesh->LockVertexBuffer(D3DLOCK_READONLY, (VOID**)&src);
			boneMesh->MeshData.pMesh->LockVertexBuffer(0, (VOID**)&dest);

			boneMesh->pSkinInfo->UpdateSkinnedMesh(boneMesh->currentBoneMatrices, NULL, src, dest);

			boneMesh->MeshData.pMesh->UnlockVertexBuffer();
			boneMesh->OriginalMesh->UnlockVertexBuffer();

			//Render the mesh
			for(int i=0;i < boneMesh->NumAttributeGroups;i++)
			{
				int mtrlIndex = boneMesh->attributeTable[i].AttribId;
				m_pDevice->SetMaterial(&(boneMesh->materials[mtrlIndex]));
				m_pDevice->SetTexture(0, boneMesh->textures[mtrlIndex]);
				boneMesh->MeshData.pMesh->DrawSubset(mtrlIndex);
			}
		}
	}

	if(bone->pFrameSibling != NULL)Render((BONE*)bone->pFrameSibling);
	if(bone->pFrameFirstChild != NULL)Render((BONE*)bone->pFrameFirstChild);
}

void SKINNEDMESH::RenderSkeleton(BONE* bone, BONE *parent, D3DXMATRIX world)
{
	//Temporary function to render the bony hierarchy

	if(world == NULL)return;
	if(bone == NULL)bone = (BONE*)m_pRootBone;

	//Draw Sphere
	m_pDevice->SetRenderState(D3DRS_LIGHTING, true);
	m_pDevice->SetTransform(D3DTS_WORLD, &(bone->CombinedTransformationMatrix * world));
	if(bone->Name != NULL)m_pSphereMesh->DrawSubset(0);

	//Draw line between bones
	if(parent != NULL && bone->Name != NULL && parent->Name != NULL)
	{
		D3DXMATRIX w1 = bone->CombinedTransformationMatrix;
		D3DXMATRIX w2 = parent->CombinedTransformationMatrix;

		//Extract translation
		D3DXVECTOR3 thisBone = D3DXVECTOR3(w1(3, 0), w1(3, 1), w1(3, 2));
		D3DXVECTOR3 ParentBone = D3DXVECTOR3(w2(3, 0), w2(3, 1), w2(3, 2));

		if(D3DXVec3Length(&(thisBone - ParentBone)) < 2.0f)
		{
			m_pDevice->SetTransform(D3DTS_WORLD, &world);
			VERTEX vert[] = {VERTEX(ParentBone, 0xffff0000), VERTEX(thisBone, 0xff00ff00)};
			m_pDevice->SetRenderState(D3DRS_LIGHTING, false);
			m_pDevice->SetFVF(VERTEX::FVF);
			m_pDevice->DrawPrimitiveUP(D3DPT_LINESTRIP, 1, &vert[0], sizeof(VERTEX));
		}
	}

	if(bone->pFrameSibling)RenderSkeleton((BONE*)bone->pFrameSibling, parent, world);
	if(bone->pFrameFirstChild)RenderSkeleton((BONE*)bone->pFrameFirstChild, bone, world);
}

void SKINNEDMESH::SetupBoneMatrixPointers(BONE *bone)
{
	if(bone->pMeshContainer != NULL)
	{
		BONEMESH *boneMesh = (BONEMESH*)bone->pMeshContainer;

		if(boneMesh->pSkinInfo != NULL)
		{
			int NumBones = boneMesh->pSkinInfo->GetNumBones();
			boneMesh->boneMatrixPtrs = new D3DXMATRIX*[NumBones];

			for(int i=0;i < NumBones;i++)
			{
				BONE *b = (BONE*)D3DXFrameFind(m_pRootBone, boneMesh->pSkinInfo->GetBoneName(i));
				if(b != NULL)boneMesh->boneMatrixPtrs[i] = &b->CombinedTransformationMatrix;
				else boneMesh->boneMatrixPtrs[i] = NULL;
			}
		}
	}

	if(bone->pFrameSibling != NULL)SetupBoneMatrixPointers((BONE*)bone->pFrameSibling);
	if(bone->pFrameFirstChild != NULL)SetupBoneMatrixPointers((BONE*)bone->pFrameFirstChild);
}

void SKINNEDMESH::SetPose(D3DXMATRIX world, ID3DXAnimationController* animControl, float time)
{
	if(animControl != NULL)
		animControl->AdvanceTime(time, NULL);
	else m_pAnimControl->AdvanceTime(time, NULL);

	UpdateMatrices((BONE*)m_pRootBone, &world);
}

void SKINNEDMESH::SetAnimation(char name[])
{
	ID3DXAnimationSet *anim = NULL;

	for(int i=0;i<m_pAnimControl->GetMaxNumAnimationSets();i++)
	{
		anim = NULL;
		m_pAnimControl->GetAnimationSet(i, &anim);

		if(anim != NULL)
		{
			if(strcmp(name, anim->GetName()) == 0)
				m_pAnimControl->SetTrackAnimationSet(0, anim);
			anim->Release();
		}
	}
}

std::vector<std::string> SKINNEDMESH::GetAnimations()
{
	ID3DXAnimationSet *anim = NULL;
	std::vector<std::string> animations;

	for(int i=0;i<m_pAnimControl->GetMaxNumAnimationSets();i++)
	{
		anim = NULL;
		m_pAnimControl->GetAnimationSet(i, &anim);

		if(anim != NULL)
		{
			animations.push_back(anim->GetName());
			anim->Release();
		}
	}

	return animations;
}