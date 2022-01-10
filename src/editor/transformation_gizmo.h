#pragma once

#include "core/math.h"
#include "core/input.h"
#include "core/camera.h"
#include "rendering/render_pass.h"


struct transformation_gizmo
{
	bool manipulateTransformation(trs& transform, const render_camera& camera, const user_input& input, bool allowInput, ldr_render_pass* ldrRenderPass);
	bool manipulatePosition(vec3& position, const render_camera& camera, const user_input& input, bool allowInput, ldr_render_pass* ldrRenderPass);
	bool manipulatePositionRotation(vec3& position, quat& rotation, const render_camera& camera, const user_input& input, bool allowInput, ldr_render_pass* ldrRenderPass);
	bool manipulateNothing(const render_camera& camera, const user_input& input, bool allowInput, ldr_render_pass* ldrRenderPass);

	// Transform before start of dragging. Only valid if object was dragged.
	trs originalTransform;

	bool dragging = false;

private:
	bool handleUserInput(bool allowKeyboardInput, bool allowTranslation, bool allowRotation, bool allowScaling, bool allowSpaceChange);
	void manipulateInternal(trs& transform, const render_camera& camera, const user_input& input, bool allowInput, ldr_render_pass* ldrRenderPass);

	enum transformation_type
	{
		transformation_type_none = -1,
		transformation_type_translation,
		transformation_type_rotation,
		transformation_type_scale,
	};

	enum transformation_space
	{
		transformation_global,
		transformation_local,
	};

	transformation_type type = transformation_type_translation;
	transformation_space space = transformation_global;

	uint32 handleTranslation(trs& transform, ray r, const user_input& input, float snapping, float scaling);
	uint32 handleRotation(trs& transform, ray r, const user_input& input, float snapping, float scaling);
	uint32 handleScaling(trs& transform, ray r, const user_input& input, float scaling, const render_camera& camera);

	uint32 axisIndex;
	float anchor;
	vec3 anchor3;
	vec4 plane;

	vec3 originalPosition;
	quat originalRotation;
	vec3 originalScale;
};

void initializeTransformationGizmos();

