#include "TESObjectREFR.h"
#include <algorithm>
#include <functional>
#include <set>
#include "BSExtraData/BSExtraData.h"
#include "Util/GeneralUtil.h"
#include "EditorIDCache.h"
#include "Util/FilePathUtil.h"
#include "TranslationCache.h"
#include "globals.h"
#include "Util/NameUtil.h"
#include "TESForm.h"

//4-30-2022: Checked for translations needed

void GetReferenceFormData(ExtraInfoEntry* resultArray, RE::TESObjectREFR* refForm)
{
	logger::debug("GetReferenceFormData Start");

	if (!MICOptions::DisableEditorIDs)
	{
		//get the editor id if there is one
		auto editorIDCache = EditorIDCache::GetSingleton();
		std::string editorID = editorIDCache->GetEditorID(refForm);

		if (editorID != "")
		{
			ExtraInfoEntry* referenceEditorIDEntry;
			CreateExtraInfoEntry(referenceEditorIDEntry, GetTranslation("$ReferenceEditorID"), editorID, priority_EditorIDReference);
			resultArray->PushBack(referenceEditorIDEntry);
		}
	}

	GetPositionData(resultArray, refForm);
	GetTextures(resultArray, refForm);
	GetMeshes(resultArray, refForm);

	//Check if the refrence is Enabled
	bool isDisabled = refForm->IsDisabled();

	ExtraInfoEntry* enabledStatusEntry;
	std::string enabledStatus = BooleanToYesNoString(!isDisabled);
	CreateExtraInfoEntry(enabledStatusEntry, GetTranslation("$ReferenceIsEnabled"), enabledStatus, priority_Reference_Enabled);

	resultArray->PushBack(enabledStatusEntry);

	GetBSExtraData(resultArray, refForm);
}

void GetPositionData(ExtraInfoEntry* resultArray, RE::TESObjectREFR* refForm)
{
	logger::debug("Starting GetPositionData");

	ExtraInfoEntry* positionEntry;
	CreateExtraInfoEntry(positionEntry, GetTranslation("$ReferencePosition"), "", priority_Reference_Location);

	//position
	float xPos = refForm->data.location.x;
	ExtraInfoEntry* xPositionEntry;
	CreateExtraInfoEntry(xPositionEntry, GetTranslation("$ReferencePositionX"), FloatToString(xPos), priority_Position_XCoordinate);
	positionEntry->PushBack(xPositionEntry);

	float yPos = refForm->data.location.y;
	ExtraInfoEntry* yPositionEntry;
	CreateExtraInfoEntry(yPositionEntry, GetTranslation("$ReferencePositionY"), FloatToString(yPos), priority_Position_YCoordinate);
	positionEntry->PushBack(yPositionEntry);

	float zPos = refForm->data.location.z;
	ExtraInfoEntry* zPositionEntry;
	CreateExtraInfoEntry(zPositionEntry, GetTranslation("$ReferencePositionZ"), FloatToString(zPos), priority_Position_ZCoordinate);
	positionEntry->PushBack(zPositionEntry);

	//rotation
	float xRot = refForm->data.angle.x;
	ExtraInfoEntry* xRotationEntry;
	CreateExtraInfoEntry(xRotationEntry, GetTranslation("$ReferenceRotationX"), RadianToDegreeString(xRot), priority_Position_XRotation);
	positionEntry->PushBack(xRotationEntry);

	float yRot = refForm->data.angle.y;
	ExtraInfoEntry* yRotationEntry;
	CreateExtraInfoEntry(yRotationEntry, GetTranslation("$ReferenceRotationY"), RadianToDegreeString(yRot), priority_Position_YRotation);
	positionEntry->PushBack(yRotationEntry);

	float zRot = refForm->data.angle.z;
	ExtraInfoEntry* zRotationEntry;
	CreateExtraInfoEntry(zRotationEntry, GetTranslation("$ReferenceRotationZ"), RadianToDegreeString(zRot), priority_Position_ZRotation);
	positionEntry->PushBack(zRotationEntry);

	resultArray->PushBack(positionEntry);

	logger::debug("Ending GetPositionData");
}


void GetTextures(ExtraInfoEntry* resultArray, RE::TESObjectREFR* refForm)
{
	logger::debug("Starting GetTextures");
	auto reference3d = refForm->Get3D();

	if (reference3d)
	{
		std::set<std::string> uniqueFilePaths;
		//vist each geometry
		RE::BSVisit::TraverseScenegraphGeometries(reference3d, [&](RE::BSGeometry* a_geometry) -> RE::BSVisit::BSVisitControl
		{
			//go from the geometry object to the texture set if it has one
			const auto effect = a_geometry->GetGeometryRuntimeData().properties[RE::BSGeometry::States::kEffect];
			const auto lightingShader = netimmerse_cast<RE::BSLightingShaderProperty*>(effect.get());
			if (lightingShader)
			{
				const auto material = static_cast<RE::BSLightingShaderMaterialBase*>(lightingShader->material);
				if (material)
				{
					if (const auto textureSet = material->textureSet; textureSet)
					{
						//there are 8 distinct texture categories
						for (int i = 0; i < 8; i++)
						{
							auto sourcePath = textureSet->GetTexturePath((RE::BSTextureSet::Texture)i);

							if (sourcePath)
							{
								uniqueFilePaths.insert(sourcePath);
							}
						}
					}
				}
			}

			return RE::BSVisit::BSVisitControl::kContinue;
		});

		if (!uniqueFilePaths.empty())
		{
			ExtraInfoEntry* texturesEntry;
			CreateExtraInfoEntry(texturesEntry, GetTranslation("$Textures"), "", priority_TextureSet);

			for (auto itr = uniqueFilePaths.begin(); itr != uniqueFilePaths.end(); itr++)
			{
				ExtraInfoEntry* textureEntry;
				std::string filePath = *itr;
				std::string fileName = GetFileName(filePath);

				CreateExtraInfoEntry(textureEntry, fileName, "", priority_Texture);
				CreateFilePathSubarray(textureEntry, filePath);
				texturesEntry->PushBack(textureEntry);
			}

			resultArray->PushBack(texturesEntry);
		}
	}

	logger::debug("Ending GetTextures");
}


//Helpers for GetMeshes
namespace
{
	std::string ToLowerCopy(std::string a_string)
	{
		std::transform(a_string.begin(), a_string.end(), a_string.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
		return a_string;
	}

	//Model paths in records are relative to the meshes folder ("armor\foo\bar_1.nif"). Make that explicit so the path matches what is on disk.
	std::string GetFullMeshPath(const std::string& a_modelPath)
	{
		std::string lower = ToLowerCopy(a_modelPath);
		if (lower.rfind("meshes\\", 0) == 0 || lower.rfind("meshes/", 0) == 0) {
			return a_modelPath;
		}
		return "Meshes\\" + a_modelPath;
	}

	//Creates one mesh entry: file name, full path split into folders, and optional detail lines
	ExtraInfoEntry* CreateMeshEntry(const std::string& a_modelPath, const std::string& a_description)
	{
		std::string fullPath = GetFullMeshPath(a_modelPath);

		ExtraInfoEntry* meshEntry;
		CreateExtraInfoEntry(meshEntry, GetFileName(fullPath), a_description, priority_Mesh);

		ExtraInfoEntry* pathEntry;
		CreateExtraInfoEntry(pathEntry, GetTranslation("$ModelPath"), "", priority_Mesh_Detail);
		CreateFilePathSubarray(pathEntry, fullPath);
		meshEntry->PushBack(pathEntry);

		return meshEntry;
	}

	void AddDetail(ExtraInfoEntry* a_entry, const std::string& a_label, const std::string& a_value)
	{
		ExtraInfoEntry* detailEntry;
		CreateExtraInfoEntry(detailEntry, a_label, a_value, priority_Mesh_Detail);
		a_entry->PushBack(detailEntry);
	}

	void AddFormDetail(ExtraInfoEntry* a_entry, const std::string& a_label, RE::TESForm* a_form)
	{
		if (a_form) {
			ExtraInfoEntry* formEntry;
			CreateExtraInfoEntry(formEntry, a_label, GetName(a_form), priority_Mesh_Detail);
			GetFormData(formEntry, a_form, nullptr);
			a_entry->PushBack(formEntry);
		}
	}

	//Equipped armor, armor addons, weapons and the race skin (body/hands/feet). The biped object holds the exact TESModel the game picked
	//for this actor's sex and race, so this is the mesh that was actually loaded rather than every option on the record.
	void GetBipedMeshes(ExtraInfoEntry* a_meshesEntry, RE::Actor* a_actor)
	{
		const auto& biped = a_actor->GetBiped(false);  //third person biped
		if (!biped) {
			return;
		}

		std::set<std::string> seenPaths;

		for (int slot = 0; slot < RE::BIPED_OBJECTS::kTotal; slot++) {
			const RE::BIPOBJECT& bipedObject = biped->objects[slot];

			if (!bipedObject.part) {
				continue;
			}

			const char* modelPathRaw = bipedObject.part->GetModel();
			if (!modelPathRaw || modelPathRaw[0] == '\0') {
				continue;
			}

			std::string modelPath = modelPathRaw;
			std::string key = ToLowerCopy(modelPath);

			//An armor that covers several slots shows up once per slot. Only list each mesh once.
			if (seenPaths.contains(key)) {
				continue;
			}
			seenPaths.insert(key);

			std::string itemName = bipedObject.item ? GetName(bipedObject.item) : "";
			ExtraInfoEntry* meshEntry = CreateMeshEntry(modelPath, itemName);

			//Biped object indexes 0-31 are the armor slots 30-61 seen in xEdit/the Creation Kit. Higher indexes are weapon/quiver positions.
			if (slot < RE::BIPED_OBJECTS::kEditorTotal) {
				AddDetail(meshEntry, GetTranslation("$MeshBipedSlot"), std::to_string(slot + 30));
			}

			if (bipedObject.partClone && !bipedObject.partClone->name.empty()) {
				AddDetail(meshEntry, GetTranslation("$MeshLoadedNode"), bipedObject.partClone->name.c_str());
			}
			else {
				AddDetail(meshEntry, GetTranslation("$MeshLoadedNode"), GetTranslation("$MeshNotLoaded"));
			}

			AddFormDetail(meshEntry, GetTranslation("$MeshItem"), bipedObject.item);
			AddFormDetail(meshEntry, GetTranslation("$Arma"), bipedObject.addon);

			a_meshesEntry->PushBack(meshEntry);
		}
	}

	//Face meshes. NPCs normally use a pre-generated FaceGen nif named after the NPC whose face they use. The player (and NPCs whose
	//FaceGen file is missing) build the head from head parts at runtime instead, so list those too.
	void GetHeadMeshes(ExtraInfoEntry* a_meshesEntry, RE::Actor* a_actor)
	{
		RE::TESNPC* npc = a_actor->GetActorBase();
		if (!npc) {
			return;
		}

		bool isPlayer = a_actor == RE::PlayerCharacter::GetSingleton();

		//Follow template chains: faceNPC points at the NPC that provides the face
		RE::TESNPC* faceSource = npc;
		int guard = 0;
		while (faceSource->faceNPC && faceSource->faceNPC != faceSource && guard++ < 16) {
			faceSource = faceSource->faceNPC;
		}

		if (!isPlayer) {

			RE::TESFile* file = faceSource->GetFile(0);
			if (file) {
				char localFormID[9];
				snprintf(localFormID, sizeof(localFormID), "%08X", faceSource->GetLocalFormID());

				std::string faceGenPath = "Actors\\Character\\FaceGenData\\FaceGeom\\" + std::string(file->GetFilename()) + "\\" + localFormID + ".nif";

				ExtraInfoEntry* faceGenEntry = CreateMeshEntry(faceGenPath, GetTranslation("$MeshFaceGen"));
				AddFormDetail(faceGenEntry, GetTranslation("$MeshFaceSource"), faceSource);
				a_meshesEntry->PushBack(faceGenEntry);
			}
		}

		//Head parts (and their extra parts) with their model paths
		ExtraInfoEntry* headPartsEntry;
		CreateExtraInfoEntry(headPartsEntry, GetTranslation("$MeshHeadParts"), "", priority_Mesh);

		std::set<RE::BGSHeadPart*> seenParts;
		std::function<void(RE::BGSHeadPart*)> addHeadPart = [&](RE::BGSHeadPart* headPart) {
			if (!headPart || seenParts.contains(headPart)) {
				return;
			}
			seenParts.insert(headPart);

			const char* modelPathRaw = headPart->GetModel();
			if (modelPathRaw && modelPathRaw[0] != '\0') {
				ExtraInfoEntry* partEntry = CreateMeshEntry(modelPathRaw, GetName(headPart));
				AddFormDetail(partEntry, GetTranslation("$MeshHeadPart"), headPart);
				headPartsEntry->PushBack(partEntry);
			}

			for (auto* extraPart : headPart->extraParts) {
				addHeadPart(extraPart);
			}
		};

		if (faceSource->headParts) {
			for (int i = 0; i < faceSource->numHeadParts; i++) {
				addHeadPart(faceSource->headParts[i]);
			}
		}

		if (headPartsEntry->HasChildren()) {
			a_meshesEntry->PushBack(headPartsEntry);
		}
		else {
			delete headPartsEntry;
		}
	}

	//Every shape that is actually in the loaded 3D right now, by shape name and the node it is attached under.
	//This catches anything that changed the 3D at runtime (body/face replacers, RaceMenu, SKSE mesh swaps) that the record data won't show.
	void GetLoadedShapes(ExtraInfoEntry* a_meshesEntry, RE::TESObjectREFR* a_refForm)
	{
		auto reference3d = a_refForm->Get3D();
		if (!reference3d) {
			return;
		}

		std::set<std::string> shapes;
		RE::BSVisit::TraverseScenegraphGeometries(reference3d, [&](RE::BSGeometry* a_geometry) -> RE::BSVisit::BSVisitControl {
			std::string shapeName = a_geometry->name.empty() ? "(unnamed)" : a_geometry->name.c_str();
			std::string parentName = (a_geometry->parent && !a_geometry->parent->name.empty()) ? a_geometry->parent->name.c_str() : "";
			shapes.insert(parentName.empty() ? shapeName : parentName + " > " + shapeName);
			return RE::BSVisit::BSVisitControl::kContinue;
		});

		if (!shapes.empty()) {
			ExtraInfoEntry* shapesEntry;
			CreateExtraInfoEntry(shapesEntry, GetTranslation("$MeshLoadedShapes"), std::to_string(shapes.size()), priority_Mesh_Detail);

			for (const auto& shape : shapes) {
				ExtraInfoEntry* shapeEntry;
				CreateExtraInfoEntry(shapeEntry, shape, "", priority_Mesh_Detail);
				shapesEntry->PushBack(shapeEntry);
			}

			a_meshesEntry->PushBack(shapesEntry);
		}
	}
}

void GetMeshes(ExtraInfoEntry* resultArray, RE::TESObjectREFR* refForm)
{
	logger::debug("Starting GetMeshes");

	if (!refForm) {
		return;
	}

	ExtraInfoEntry* meshesEntry;
	CreateExtraInfoEntry(meshesEntry, GetTranslation("$Meshes"), "", priority_Meshes);

	RE::Actor* actor = refForm->As<RE::Actor>();

	if (actor) {
		GetBipedMeshes(meshesEntry, actor);
		GetHeadMeshes(meshesEntry, actor);
	}
	else {
		//Non actors: the base object's model
		RE::TESBoundObject* baseObject = refForm->GetBaseObject();
		RE::TESModel* model = baseObject ? baseObject->As<RE::TESModel>() : nullptr;
		if (model && model->GetModel() && model->GetModel()[0] != '\0') {
			meshesEntry->PushBack(CreateMeshEntry(model->GetModel(), ""));
		}
	}

	GetLoadedShapes(meshesEntry, refForm);

	if (meshesEntry->HasChildren()) {
		resultArray->PushBack(meshesEntry);
	}
	else {
		delete meshesEntry;
	}

	logger::debug("Ending GetMeshes");
}
