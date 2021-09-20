#include "pch.h"
#include "animation.h"
#include "core/assimp.h"
#include "core/imgui.h"
#include "geometry/mesh.h"
#include "skinning.h"

static void readJointAnimation(animation_clip& clip, animation_joint& joint, const aiNodeAnim* channel)
{
	joint.firstPositionKeyframe = (uint32)clip.positionKeyframes.size();
	joint.firstRotationKeyframe = (uint32)clip.rotationKeyframes.size();
	joint.firstScaleKeyframe = (uint32)clip.scaleKeyframes.size();

	joint.numPositionKeyframes = channel->mNumPositionKeys;
	joint.numRotationKeyframes = channel->mNumRotationKeys;
	joint.numScaleKeyframes = channel->mNumScalingKeys;


	for (uint32 keyID = 0; keyID < channel->mNumPositionKeys; ++keyID)
	{
		clip.positionKeyframes.push_back(readAssimpVector(channel->mPositionKeys[keyID].mValue));
		clip.positionTimestamps.push_back((float)channel->mPositionKeys[keyID].mTime * 0.001f);
	}

	for (uint32 keyID = 0; keyID < channel->mNumRotationKeys; ++keyID)
	{
		clip.rotationKeyframes.push_back(readAssimpQuaternion(channel->mRotationKeys[keyID].mValue));
		clip.rotationTimestamps.push_back((float)channel->mRotationKeys[keyID].mTime * 0.001f);
	}

	for (uint32 keyID = 0; keyID < channel->mNumScalingKeys; ++keyID)
	{
		clip.scaleKeyframes.push_back(readAssimpVector(channel->mScalingKeys[keyID].mValue));
		clip.scaleTimestamps.push_back((float)channel->mScalingKeys[keyID].mTime * 0.001f);
	}

	joint.isAnimated = true;
}

static void scaleKeyframes(animation_clip& clip, animation_joint& joint, float scale)
{
	for (uint32 keyID = 0; keyID < joint.numPositionKeyframes; ++keyID)
	{
		clip.positionKeyframes[joint.firstPositionKeyframe + keyID] *= scale;
	}
	for (uint32 keyID = 0; keyID < joint.numScaleKeyframes; ++keyID)
	{
		clip.scaleKeyframes[joint.firstScaleKeyframe + keyID] *= scale;
	}
}

void animation_skeleton::pushAssimpAnimation(const fs::path& sceneFilename, const aiAnimation* animation, float scale)
{
	animation_clip& clip = clips.emplace_back();

	clip.name = animation->mName.C_Str();
	size_t posOfFirstOr = clip.name.find_last_of('|');
	if (posOfFirstOr != std::string::npos)
	{
		clip.name = clip.name.substr(posOfFirstOr + 1);
	}

	clip.name += " (" + sceneFilename.filename().string() + ")";

	clip.filename = sceneFilename;

	clip.lengthInSeconds = (float)animation->mDuration * 0.001f;

	float timeNormalization = 1.f / (float)animation->mTicksPerSecond;

	clip.joints.resize(joints.size());
	clip.positionKeyframes.clear();
	clip.rotationKeyframes.clear();
	clip.scaleKeyframes.clear();

	for (uint32 channelID = 0; channelID < animation->mNumChannels; ++channelID)
	{
		const aiNodeAnim* channel = animation->mChannels[channelID];
		std::string jointName = channel->mNodeName.C_Str();

		auto it = nameToJointID.find(jointName);
		if (it != nameToJointID.end())
		{
			animation_joint& joint = clip.joints[it->second];
			readJointAnimation(clip, joint, channel);
		}
		else if (jointName == "root")
		{
			readJointAnimation(clip, clip.rootMotionJoint, channel);
		}
	}

	if (clip.rootMotionJoint.isAnimated)
	{
		scaleKeyframes(clip, clip.rootMotionJoint, scale);
	}
	else
	{
		for (uint32 i = 0; i < (uint32)joints.size(); ++i)
		{
			if (joints[i].parentID == NO_PARENT)
			{
				scaleKeyframes(clip, clip.joints[i], scale);
			}
		}
	}
}

void animation_skeleton::pushAssimpAnimations(const fs::path& sceneFilename, float scale)
{
	Assimp::Importer importer;

	const aiScene* scene = loadAssimpSceneFile(sceneFilename, importer);

	if (scene)
	{
		for (uint32 i = 0; i < scene->mNumAnimations; ++i)
		{
			pushAssimpAnimation(sceneFilename, scene->mAnimations[i], scale);
		}
		files.push_back(sceneFilename);
	}
}

void animation_skeleton::pushAssimpAnimationsInDirectory(const fs::path& directory, float scale)
{
	for (auto& p : fs::directory_iterator(directory))
	{
		pushAssimpAnimations(p.path(), scale);
	}
}

static void readAssimpSkeletonHierarchy(const aiNode* node, animation_skeleton& skeleton, uint32& insertIndex, uint32 parentID = NO_PARENT)
{
	std::string name = node->mName.C_Str();

	if (name == "Animation") // TODO: Temporary fix for the pilot.fbx mesh.
	{
		return;
	}

	auto it = skeleton.nameToJointID.find(name);
	if (it != skeleton.nameToJointID.end())
	{
		uint32 jointID = it->second;

		skeleton.joints[jointID].parentID = parentID;

		// This sorts the joints, such that parents are before their children.
		skeleton.nameToJointID[name] = insertIndex;
		skeleton.nameToJointID[skeleton.joints[insertIndex].name] = jointID;
		std::swap(skeleton.joints[jointID], skeleton.joints[insertIndex]);

		parentID = insertIndex;

		++insertIndex;
	}

	for (uint32 i = 0; i < node->mNumChildren; ++i)
	{
		readAssimpSkeletonHierarchy(node->mChildren[i], skeleton, insertIndex, parentID);
	}
}

void animation_skeleton::loadFromAssimp(const aiScene* scene, float scale)
{
	mat4 scaleMatrix = mat4::identity * (1.f / scale);
	scaleMatrix.m33 = 1.f;

	for (uint32 meshID = 0; meshID < scene->mNumMeshes; ++meshID)
	{
		const aiMesh* mesh = scene->mMeshes[meshID];

		for (uint32 boneID = 0; boneID < mesh->mNumBones; ++boneID)
		{
			const aiBone* bone = mesh->mBones[boneID];
			std::string name = bone->mName.C_Str();

			auto it = nameToJointID.find(name);
			if (it == nameToJointID.end())
			{
				nameToJointID[name] = (uint32)joints.size();

				skeleton_joint& joint = joints.emplace_back();
				joint.name = std::move(name);
				joint.invBindTransform = readAssimpMatrix(bone->mOffsetMatrix) * scaleMatrix;
				joint.bindTransform = invert(joint.invBindTransform);
			}
#if 0
			else
			{
				mat4 invBind = readAssimpMatrix(bone->mOffsetMatrix) * scaleMatrix;
				assert(invBind == joints[it->second].invBindMatrix);
			}
#endif
		}
	}

	uint32 insertIndex = 0;
	readAssimpSkeletonHierarchy(scene->mRootNode, *this, insertIndex);
}

static vec3 samplePosition(const animation_clip& clip, const animation_joint& animJoint, float time)
{
	if (time >= clip.lengthInSeconds)
	{
		return clip.positionKeyframes[animJoint.firstPositionKeyframe + animJoint.numPositionKeyframes - 1];
	}

	if (animJoint.numPositionKeyframes == 1)
	{
		return clip.positionKeyframes[animJoint.firstPositionKeyframe];
	}

	uint32 firstKeyframeIndex = -1;
	for (uint32 i = 0; i < animJoint.numPositionKeyframes - 1; ++i)
	{
		uint32 j = i + animJoint.firstPositionKeyframe;
		if (time < clip.positionTimestamps[j + 1])
		{
			firstKeyframeIndex = j;
			break;
		}
	}
	assert(firstKeyframeIndex != -1);

	uint32 secondKeyframeIndex = firstKeyframeIndex + 1;

	float t = inverseLerp(clip.positionTimestamps[firstKeyframeIndex], clip.positionTimestamps[secondKeyframeIndex], time);

	vec3 a = clip.positionKeyframes[firstKeyframeIndex];
	vec3 b = clip.positionKeyframes[secondKeyframeIndex];

	return lerp(a, b, t);
}

static quat sampleRotation(const animation_clip& clip, const animation_joint& animJoint, float time)
{
	if (time >= clip.lengthInSeconds)
	{
		return clip.rotationKeyframes[animJoint.firstRotationKeyframe + animJoint.numRotationKeyframes - 1];
	}

	if (animJoint.numRotationKeyframes == 1)
	{
		return clip.rotationKeyframes[animJoint.firstRotationKeyframe];
	}

	uint32 firstKeyframeIndex = -1;
	for (uint32 i = 0; i < animJoint.numRotationKeyframes - 1; ++i)
	{
		uint32 j = i + animJoint.firstRotationKeyframe;
		if (time < clip.rotationTimestamps[j + 1])
		{
			firstKeyframeIndex = j;
			break;
		}
	}
	assert(firstKeyframeIndex != -1);

	uint32 secondKeyframeIndex = firstKeyframeIndex + 1;

	float t = inverseLerp(clip.rotationTimestamps[firstKeyframeIndex], clip.rotationTimestamps[secondKeyframeIndex], time);

	quat a = clip.rotationKeyframes[firstKeyframeIndex];
	quat b = clip.rotationKeyframes[secondKeyframeIndex];

	if (dot(a.v4, b.v4) < 0.f)
	{
		b.v4 *= -1.f;
	}

	return lerp(a, b, t);
}

static vec3 sampleScale(const animation_clip& clip, const animation_joint& animJoint, float time)
{
	if (time >= clip.lengthInSeconds)
	{
		return clip.scaleKeyframes[animJoint.firstScaleKeyframe + animJoint.numScaleKeyframes - 1];
	}

	if (animJoint.numScaleKeyframes == 1)
	{
		return clip.scaleKeyframes[animJoint.firstScaleKeyframe];
	}

	uint32 firstKeyframeIndex = -1;
	for (uint32 i = 0; i < animJoint.numScaleKeyframes - 1; ++i)
	{
		uint32 j = i + animJoint.firstScaleKeyframe;
		if (time < clip.scaleTimestamps[j + 1])
		{
			firstKeyframeIndex = j;
			break;
		}
	}
	assert(firstKeyframeIndex != -1);

	uint32 secondKeyframeIndex = firstKeyframeIndex + 1;

	float t = inverseLerp(clip.scaleTimestamps[firstKeyframeIndex], clip.scaleTimestamps[secondKeyframeIndex], time);

	vec3 a = clip.scaleKeyframes[firstKeyframeIndex];
	vec3 b = clip.scaleKeyframes[secondKeyframeIndex];

	return lerp(a, b, t);
}

void animation_skeleton::sampleAnimation(const animation_clip& clip, float time, trs* outLocalTransforms, trs* outRootMotion) const
{
	assert(clip.joints.size() == joints.size());

	time = clamp(time, 0.f, clip.lengthInSeconds);

	uint32 numJoints = (uint32)joints.size();
	for (uint32 i = 0; i < numJoints; ++i)
	{
		const animation_joint& animJoint = clip.joints[i];

		if (animJoint.isAnimated)
		{
			outLocalTransforms[i].position = samplePosition(clip, animJoint, time);
			outLocalTransforms[i].rotation = sampleRotation(clip, animJoint, time);
			outLocalTransforms[i].scale = sampleScale(clip, animJoint, time);
		}
		else
		{
			outLocalTransforms[i] = trs::identity;
		}
	}

	trs rootMotion;
	if (clip.rootMotionJoint.isAnimated)
	{
		rootMotion.position = samplePosition(clip, clip.rootMotionJoint, time);
		rootMotion.rotation = sampleRotation(clip, clip.rootMotionJoint, time);
		rootMotion.scale = sampleScale(clip, clip.rootMotionJoint, time);
	}
	else
	{
		rootMotion = trs::identity;
	}

	if (outRootMotion)
	{
		if (clip.bakeRootRotationIntoPose)
		{
			outLocalTransforms[0] = trs(0.f, rootMotion.rotation) * outLocalTransforms[0];
			rootMotion.rotation = quat::identity;
		}

		if (clip.bakeRootXZTranslationIntoPose)
		{
			outLocalTransforms[0].position.x += rootMotion.position.x;
			outLocalTransforms[0].position.z += rootMotion.position.z;
			rootMotion.position.x = 0.f;
			rootMotion.position.z = 0.f;
		}

		if (clip.bakeRootYTranslationIntoPose)
		{
			outLocalTransforms[0].position.y += rootMotion.position.y;
			rootMotion.position.y = 0.f;
		}

		*outRootMotion = rootMotion;
	}
	else
	{
		outLocalTransforms[0] = rootMotion * outLocalTransforms[0];
	}
}

void animation_skeleton::sampleAnimation(uint32 index, float time, trs* outLocalTransforms, trs* outRootMotion) const
{
	sampleAnimation(clips[index], time, outLocalTransforms, outRootMotion);
}

void animation_skeleton::blendLocalTransforms(const trs* localTransforms1, const trs* localTransforms2, float t, trs* outBlendedLocalTransforms) const
{
	t = clamp01(t);
	for (uint32 jointID = 0; jointID < (uint32)joints.size(); ++jointID)
	{
		outBlendedLocalTransforms[jointID] = lerp(localTransforms1[jointID], localTransforms2[jointID], t);
	}
}

void animation_skeleton::getSkinningMatricesFromLocalTransforms(const trs* localTransforms, mat4* outSkinningMatrices, const trs& worldTransform) const
{
	uint32 numJoints = (uint32)joints.size();
	trs* globalTransforms = (trs*)alloca(sizeof(trs) * numJoints);

	for (uint32 i = 0; i < numJoints; ++i)
	{
		const skeleton_joint& skelJoint = joints[i];
		if (skelJoint.parentID != NO_PARENT)
		{
			assert(i > skelJoint.parentID); // Parent already processed.
			globalTransforms[i] = globalTransforms[skelJoint.parentID] * localTransforms[i];
		}
		else
		{
			globalTransforms[i] = worldTransform * localTransforms[i];
		}

		outSkinningMatrices[i] = trsToMat4(globalTransforms[i] * joints[i].invBindTransform);
	}
}

void animation_skeleton::getSkinningMatricesFromGlobalTransforms(const trs* globalTransforms, mat4* outSkinningMatrices) const
{
	uint32 numJoints = (uint32)joints.size();

	for (uint32 i = 0; i < numJoints; ++i)
	{
		outSkinningMatrices[i] = trsToMat4(globalTransforms[i] * joints[i].invBindTransform);
	}
}

std::vector<uint32> animation_skeleton::getClipsByName(const std::string& name)
{
	std::vector<uint32> result;
	for (uint32 i = 0; i < (uint32)clips.size(); ++i)
	{
		if (clips[i].name == name)
		{
			result.push_back(i);
		}
	}
	return result;
}

static void prettyPrint(const animation_skeleton& skeleton, uint32 parent, uint32 indent)
{
	for (uint32 i = 0; i < (uint32)skeleton.joints.size(); ++i)
	{
		if (skeleton.joints[i].parentID == parent)
		{
			std::cout << std::string(indent, ' ') << skeleton.joints[i].name << '\n';
			prettyPrint(skeleton, i, indent + 1);
		}
	}
}

void animation_skeleton::prettyPrintHierarchy() const
{
	prettyPrint(*this, NO_PARENT, 0);
}

void animation_clip::edit()
{
	ImGui::Checkbox("Bake root rotation into pose", &bakeRootRotationIntoPose);
	ImGui::Checkbox("Bake xz translation into pose", &bakeRootXZTranslationIntoPose);
	ImGui::Checkbox("Bake y translation into pose", &bakeRootYTranslationIntoPose);
}

trs animation_clip::getFirstRootTransform() const
{
	if (rootMotionJoint.isAnimated)
	{
		trs t;
		t.position = positionKeyframes[rootMotionJoint.firstPositionKeyframe];
		t.rotation = rotationKeyframes[rootMotionJoint.firstRotationKeyframe];
		t.scale = scaleKeyframes[rootMotionJoint.firstScaleKeyframe];

		if (bakeRootRotationIntoPose)
		{
			t.rotation = quat::identity;
		}
		if (bakeRootXZTranslationIntoPose)
		{
			t.position.x = 0.f;
			t.position.z = 0.f;
		}
		if (bakeRootYTranslationIntoPose)
		{
			t.position.y = 0.f;
		}

		return t;
	}
	return trs::identity;
}

trs animation_clip::getLastRootTransform() const
{
	if (rootMotionJoint.isAnimated)
	{
		trs t;
		t.position = positionKeyframes[rootMotionJoint.firstPositionKeyframe + rootMotionJoint.numPositionKeyframes - 1];
		t.rotation = rotationKeyframes[rootMotionJoint.firstRotationKeyframe + rootMotionJoint.numRotationKeyframes - 1];
		t.scale = scaleKeyframes[rootMotionJoint.firstScaleKeyframe + rootMotionJoint.numScaleKeyframes - 1];

		if (bakeRootRotationIntoPose)
		{
			t.rotation = quat::identity;
		}
		if (bakeRootXZTranslationIntoPose)
		{
			t.position.x = 0.f;
			t.position.z = 0.f;
		}
		if (bakeRootYTranslationIntoPose)
		{
			t.position.y = 0.f;
		}

		return t;
	}
	return trs::identity;
}

animation_instance::animation_instance(const animation_clip* clip, float startTime)
{
	set(clip, startTime);
}

void animation_instance::set(const animation_clip* clip, float startTime)
{
	this->clip = clip;
	time = startTime;
	lastRootMotion = clip->getFirstRootTransform();
}

void animation_instance::update(const animation_skeleton& skeleton, float dt, trs* outLocalTransforms, trs& outDeltaRootMotion)
{
	if (valid())
	{
		time += dt;
		if (time >= clip->lengthInSeconds)
		{
			if (clip->looping)
			{
				time = fmod(time, clip->lengthInSeconds);
				lastRootMotion = clip->getFirstRootTransform();
			}
			else
			{
				time = clip->lengthInSeconds;
			}
		}

		trs rootMotion;
		skeleton.sampleAnimation(*clip, time, outLocalTransforms, &rootMotion);

		outDeltaRootMotion = invert(lastRootMotion) * rootMotion;
		lastRootMotion = rootMotion;
	}
}

#if 0
animation_blend_tree_1d::animation_blend_tree_1d(std::initializer_list<animation_clip*> clips, float startBlendValue, float startRelTime)
{
	assert(clips.size() <= arraysize(this->clips));
	assert(clips.size() > 1);

	numClips = 0;
	for (animation_clip* clip : clips)
	{
		this->clips[numClips++] = clip;
	}

	first = 0;
	second = 1;
	relTime = startRelTime;

	setBlendValue(startBlendValue);

	lastRootMotion = lerp(this->clips[first]->getFirstRootTransform(), this->clips[second]->getFirstRootTransform(), blendValue);
}

void animation_blend_tree_1d::update(const animation_skeleton& skeleton, float dt, trs* outLocalTransforms, trs& outDeltaRootMotion)
{
	const animation_clip* first = clips[this->first];
	const animation_clip* second = clips[this->second];

	float firstLength = first->lengthInSeconds;
	float secondLength = second->lengthInSeconds;

	float lengthDist = secondLength - firstLength;
	float targetLength = firstLength + lengthDist * blendValue;

	float start = relTime;
	relTime += dt / targetLength;
	relTime = fmodf(relTime, 1.f);


	trs* totalLocalTransforms = (trs*)alloca(sizeof(trs) * skeleton.joints.size() * 2);
	trs* localTransforms1 = totalLocalTransforms;
	trs* localTransforms2 = totalLocalTransforms + skeleton.joints.size();

	trs rootMotion1, rootMotion2;
	skeleton.sampleAnimation(*first, first->lengthInSeconds * relTime, localTransforms1, &rootMotion1);
	skeleton.sampleAnimation(*second, second->lengthInSeconds * relTime, localTransforms2, &rootMotion2);

	skeleton.blendLocalTransforms(localTransforms1, localTransforms2, blendValue, outLocalTransforms);
	
	trs rootMotion = lerp(rootMotion1, rootMotion2, blendValue);

	outDeltaRootMotion = invert(lastRootMotion) * rootMotion;
	lastRootMotion = rootMotion;
}

void animation_blend_tree_1d::setBlendValue(float value)
{
	value = clamp01(value);

	float step = 1.f / (numClips - 1);

	uint32 newFirstAnimationIndex = numClips - 2, newSecondAnimationIndex = numClips - 1;
	if (value < 1.f)
	{
		newFirstAnimationIndex = (uint32)(value / step);
		newSecondAnimationIndex = newFirstAnimationIndex + 1;
	}

	first = newFirstAnimationIndex;
	second = newSecondAnimationIndex;

	float begin = newFirstAnimationIndex * step;
	this->blendValue = (value - begin) / step;
	this->value = value;
}
#endif

void animation_component::update(const ref<composite_mesh>& mesh, float dt, trs* transform)
{
	const dx_mesh& dxMesh = mesh->mesh;
	animation_skeleton& skeleton = mesh->skeleton;

	if (animation.valid())
	{
		auto [vb, skinningMatrices] = skinObject(dxMesh.vertexBuffer, dxMesh.vertexBuffer.positions->elementCount, (uint32)skeleton.joints.size());

		prevFrameVertexBuffer = currentVertexBuffer;
		currentVertexBuffer = vb;

		trs* localTransforms = (trs*)alloca(sizeof(trs) * skeleton.joints.size());
		trs deltaRootMotion;
		animation.update(skeleton, dt * timeScale, localTransforms, deltaRootMotion);

		skeleton.getSkinningMatricesFromLocalTransforms(localTransforms, skinningMatrices);

		if (transform)
		{
			*transform = *transform * deltaRootMotion;
			transform->rotation = normalize(transform->rotation);
		}
	}
	else
	{
		currentVertexBuffer = dxMesh.vertexBuffer;
		if (!prevFrameVertexBuffer)
		{
			prevFrameVertexBuffer = currentVertexBuffer;
		}
	}
}
