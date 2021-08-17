#pragma once

#include "core/math.h"
#include "core/random.h"
#include <unordered_map>

#define NO_PARENT 0xFFFFFFFF

struct skinning_weights
{
	uint8 skinIndices[4];
	uint8 skinWeights[4];
};

struct skeleton_joint
{
	std::string name;
	trs invBindTransform; // Transforms from model space to joint space.
	trs bindTransform;	  // Position of joint relative to model space.
	uint32 parentID;
};

struct animation_transition_event
{
	uint32 targetIndex;
	float targetStartTime;
	float transitionTime;
	float automaticProbability;
};

enum animation_event_type
{
	animation_event_type_transition,
};

struct animation_event
{
	float time;
	animation_event_type type;

	union
	{
		animation_transition_event transition;
	};
};

struct animation_event_indices
{
	uint32 first;
	uint32 count;
};

struct animation_joint
{
	bool isAnimated = false;

	uint32 firstPositionKeyframe;
	uint32 numPositionKeyframes;

	uint32 firstRotationKeyframe;
	uint32 numRotationKeyframes;

	uint32 firstScaleKeyframe;
	uint32 numScaleKeyframes;
};

struct animation_clip
{
	std::string name;
	std::string filename;

	std::vector<float> positionTimestamps;
	std::vector<float> rotationTimestamps;
	std::vector<float> scaleTimestamps;

	std::vector<vec3> positionKeyframes;
	std::vector<quat> rotationKeyframes;
	std::vector<vec3> scaleKeyframes;

	std::vector<animation_joint> joints;

	std::vector<animation_event> events;

	animation_joint rootMotionJoint;
	
	float lengthInSeconds;
	bool looping = true;
	bool bakeRootRotationIntoPose = false;
	bool bakeRootXZTranslationIntoPose = false;
	bool bakeRootYTranslationIntoPose = false;


	void edit();
	trs getFirstRootTransform() const;
	trs getLastRootTransform() const;
};

struct animation_skeleton
{
	std::vector<skeleton_joint> joints;
	std::unordered_map<std::string, uint32> nameToJointID;

	std::vector<animation_clip> clips;
	std::vector<std::string> files;

	void loadFromAssimp(const struct aiScene* scene, float scale = 1.f);
	void pushAssimpAnimation(const std::string& sceneFilename, const struct aiAnimation* animation, float scale = 1.f);
	void pushAssimpAnimations(const std::string& sceneFilename, float scale = 1.f);
	void pushAssimpAnimationsInDirectory(const std::string& directory, float scale = 1.f);

	void readAnimationPropertiesFromFile(const std::string& filename);

	void sampleAnimation(const animation_clip& clip, float timeNow, trs* outLocalTransforms, trs* outRootMotion = 0) const;
	void sampleAnimation(uint32 index, float timeNow, trs* outLocalTransforms, trs* outRootMotion = 0) const;
	animation_event_indices sampleAnimation(const animation_clip& clip, float prevTime, float timeNow, trs* outLocalTransforms, trs* outRootMotion = 0) const;
	animation_event_indices sampleAnimation(uint32 index, float prevTime, float timeNow, trs* outLocalTransforms, trs* outRootMotion = 0) const;
	void blendLocalTransforms(const trs* localTransforms1, const trs* localTransforms2, float t, trs* outBlendedLocalTransforms) const;
	void getSkinningMatricesFromLocalTransforms(const trs* localTransforms, mat4* outSkinningMatrices, const trs& worldTransform = trs::identity) const;
	void getSkinningMatricesFromGlobalTransforms(const trs* globalTransforms, mat4* outSkinningMatrices) const;

	std::vector<uint32> getClipsByName(const std::string& name);

	void prettyPrintHierarchy() const;
};

struct animation_instance
{
	animation_instance() { }
	animation_instance(const animation_clip* clip, float startTime = 0.f);

	animation_event_indices update(const animation_skeleton& skeleton, float dt, trs* outLocalTransforms, trs& outDeltaRootMotion);

	bool valid() const { return clip != 0; }

	const animation_clip* clip = 0;
	float time = 0.f;

	trs lastRootMotion;
};

struct animation_player
{
	animation_player() { }
	animation_player(animation_clip* clip);

	void transitionTo(const animation_clip* clip, float transitionTime, float startTime = 0.f);
	void update(const animation_skeleton& skeleton, float dt, trs* outLocalTransforms, trs& outDeltaRootMotion, bool ignoreEvents = false);

	bool playing() const { return to.valid(); }
	bool transitioning() const { return from.valid(); }

	animation_instance from; 
	animation_instance to;

	float transitionProgress = 0.f;
	float transitionTime = 0.f;

	random_number_generator rng = { 51293 };
};

#if 0
struct animation_blend_tree_1d
{
	animation_blend_tree_1d() { }
	animation_blend_tree_1d(std::initializer_list<animation_clip*> clips, float startRelTime = 0.f, float startBlendValue = 0.f);

	void setBlendValue(float blendValue);
	void update(const animation_skeleton& skeleton, float dt, trs* outLocalTransforms, trs& outDeltaRootMotion);

private:
	animation_clip* clips[8];
	uint32 numClips = 0;

	float value;
	uint32 first;
	uint32 second;
	float relTime;
	float blendValue;

	trs lastRootMotion;
};
#endif
