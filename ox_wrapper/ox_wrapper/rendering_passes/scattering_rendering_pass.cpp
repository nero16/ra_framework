#include "scattering_rendering_pass.h"
#include "../context.h"
#include "../../ptx.h"
#include "../scene_section.h"
#include "../ray_generator.h"

using namespace ox_wrapper;
using namespace ox_wrapper::rendering_passes;

namespace {

void applyMaterialAssemblyToSceneSection(OxSceneSection const& scene_section, OxMaterialAssembly const& material_assembly)
{
    for (auto& ss : scene_section.sceneSections())
        applyMaterialAssemblyToSceneSection(ss, material_assembly);

    for (auto& gg : scene_section.geometryGroups())
    {
        for (auto& g : gg.geometries())
            if (static_cast<OxMaterialAssembly const&>(g.getMaterialAssembly()).getMaterialCount() == 0)
                g.setMaterialAssembly(material_assembly);
    }
}

}

OxScatteringRenderingPass::OxScatteringRenderingPass(
    OxSceneSection const& scene_section,
    uint8_t num_spectra_pairs_supported,
    uint32_t max_recursion_depth,
    float ray_marching_step_size,
    uint32_t num_scattering_integral_importance_directions,
    OxProgram const& absorption_probability_shader,
    OxProgram const& scattering_probability_shader,
    OxProgram const& scattering_phase_function_shader) :
    OxRenderingPass{ scene_section },
    OxContractWithOxPrograms{ absorption_probability_shader, scattering_probability_shader, scattering_phase_function_shader },
    m_num_spectra_pairs_supported{ num_spectra_pairs_supported },
    //m_max_recursion_depth{ max_recursion_depth },
    //m_ray_marching_step_size{ ray_marching_step_size },
    m_num_scattering_integral_importance_directions{ num_scattering_integral_importance_directions },
    m_surface_material_assembly{
    OxMaterial{ targetSceneSection().context().createProgram(PTX_SCATTERING_RENDERING_PASS, OxProgram::Source::file, OX_SHADER_ENTRY_CLOSEST_HIT), util::Optional<OxProgram>{}, OxRayType::unknown },
    OxMaterial{ targetSceneSection().context().createProgram(PTX_SCATTERING_RENDERING_PASS, OxProgram::Source::file, "__ox_intersect_scattered__"), util::Optional<OxProgram>{}, OxRayType::scattered} },
    m_miss_shader_assembly{
    OxMissShader{ targetSceneSection().context().createProgram(PTX_SCATTERING_RENDERING_PASS, OxProgram::Source::file, OX_SHADER_ENTRY_MISS), OxRayType::unknown},
    OxMissShader{ targetSceneSection().context().createProgram(PTX_SCATTERING_RENDERING_PASS, OxProgram::Source::file, "__ox_miss_scattered__"), OxRayType::scattered} },
    m_traverse_backup_buffer{ targetSceneSection().context(), scene_section.rayGenerator().numberOfRays() },
    m_recaster_ray_generator{ m_traverse_backup_buffer, castBufferToType<OxRayRadiancePayload>(scene_section.rayGenerator().outputBuffer()), OxRayType::unknown },
    m_importance_directions_buffer{ targetSceneSection().context().createBuffer<float2>(OxBufferKind::input, num_scattering_integral_importance_directions*(1 + num_spectra_pairs_supported)) }
{
    static_cast<OxProgram&>(static_cast<OxMaterial&>(m_surface_material_assembly.getMaterialByRayType(OxRayType::unknown)).getClosestHitShader())
        .declareVariable("num_spectra_pairs_supported", m_num_spectra_pairs_supported);
    static_cast<OxProgram&>(static_cast<OxMissShader&>(m_miss_shader_assembly.getMissShaderByRayType(OxRayType::unknown)).getProgram())
        .declareVariable("num_spectra_pairs_supported", m_num_spectra_pairs_supported);

    setMaxRecursionDepth(max_recursion_depth);
    setRayMarchingStepSize(ray_marching_step_size);

    static_cast<OxProgram&>(static_cast<OxMaterial&>(m_surface_material_assembly.getMaterialByRayType(OxRayType::unknown)).getClosestHitShader())
        .declareVariable("num_importance_directions", num_scattering_integral_importance_directions);
    static_cast<OxProgram&>(static_cast<OxMissShader&>(m_miss_shader_assembly.getMissShaderByRayType(OxRayType::unknown)).getProgram())
        .declareVariable("num_importance_directions", num_scattering_integral_importance_directions);
    
    setAbsorptionProbabilityShader(absorption_probability_shader);
    setScatteringProbabilityShader(scattering_probability_shader);
    setScatteringPhaseFunctionShader(scattering_phase_function_shader);

    static_cast<OxProgram&>(static_cast<OxMaterial&>(m_surface_material_assembly.getMaterialByRayType(OxRayType::unknown)).getClosestHitShader())
        .assignBuffer("traverse_backup_buffer", m_traverse_backup_buffer.getRawBuffer());
    static_cast<OxProgram&>(static_cast<OxMaterial&>(m_surface_material_assembly.getMaterialByRayType(OxRayType::unknown)).getClosestHitShader())
        .assignBuffer("importance_directions_buffer", m_importance_directions_buffer);
    static_cast<OxMissShader&>(m_miss_shader_assembly.getMissShaderByRayType(OxRayType::unknown))
        .getProgram().assignBuffer("importance_directions_buffer", m_importance_directions_buffer);
}

OxScatteringRenderingPass::OxScatteringRenderingPass(
    OxSceneSection const& scene_section,
    uint8_t num_spectra_pairs_supported, 
    uint32_t max_recursion_depth, 
    float ray_marching_step_size, 
    uint32_t num_scattering_integral_importance_directions):
    OxScatteringRenderingPass{ scene_section, num_spectra_pairs_supported, max_recursion_depth, ray_marching_step_size, num_scattering_integral_importance_directions,
                               scene_section.context().createProgram(PTX_SCATTERING_RENDERING_PASS, OxProgram::Source::file, "__ox_scattering_material_default_absorption_factor__"),
                               scene_section.context().createProgram(PTX_SCATTERING_RENDERING_PASS, OxProgram::Source::file, "__ox_scattering_material_default_scattering_factor__"),
                               scene_section.context().createProgram(PTX_SCATTERING_RENDERING_PASS, OxProgram::Source::file, "__ox_scattering_material_default_phase_funciton__") }
{
}

float2* OxScatteringRenderingPass::mapImportanceDirectionsBuffer() const
{
    return m_importance_directions_buffer.map(OxBufferMapKind::read_write);
}

void OxScatteringRenderingPass::unmapImportanceDirectionsBuffer() const
{
    m_importance_directions_buffer.unmap();
}

uint32_t OxScatteringRenderingPass::getMaxRecursionDepth() const
{
    return m_max_recursion_depth;
}

void OxScatteringRenderingPass::setMaxRecursionDepth(uint32_t max_recursion_depth)
{
    m_max_recursion_depth = max_recursion_depth;

    for (auto& material : m_surface_material_assembly)
    {
        static_cast<OxProgram&>(material.getClosestHitShader())
            .declareVariable("max_recursion_depth", max_recursion_depth);
    }

    for (auto& miss_shader : m_miss_shader_assembly)
    {
        miss_shader.getProgram().declareVariable("max_recursion_depth", max_recursion_depth);
    }
}

uint32_t OxScatteringRenderingPass::getNumberOfScatteringIntegralImportanceDirections() const
{
    return m_num_scattering_integral_importance_directions;
}

uint32_t OxScatteringRenderingPass::getNumberOfSpectraPairsSupported() const
{
    return m_num_spectra_pairs_supported;
}

float OxScatteringRenderingPass::getRayMarchingStepSize() const
{
    return m_ray_marching_step_size;
}

void OxScatteringRenderingPass::setRayMarchingStepSize(float step_size)
{
    for (auto& ms : m_miss_shader_assembly)
    {
        ms.getProgram().declareVariable("step_size", m_ray_marching_step_size);
    }

    static_cast<OxProgram&>(
        static_cast<OxMaterial&>(m_surface_material_assembly.getMaterialByRayType(OxRayType::unknown))
        .getClosestHitShader())
        .declareVariable("step_size", m_ray_marching_step_size);
}

OxProgram OxScatteringRenderingPass::getAbsorptionProbabilityShader() const
{
    return getOxProgramFromDeclarationOffset(0U);
}

void OxScatteringRenderingPass::setAbsorptionProbabilityShader(OxProgram const& absorption_probability_shader)
{
    replaceProgramAtOffset(absorption_probability_shader, 0U);

    for (auto& ms : m_miss_shader_assembly)
    {
        ms.getProgram().declareVariable("absorption_factor", OxObjectHandle{ nativeOptiXProgramHandle(0U) });
    }

    static_cast<OxProgram&>(
        static_cast<OxMaterial&>(m_surface_material_assembly.getMaterialByRayType(OxRayType::unknown))
        .getClosestHitShader())
        .declareVariable("absorption_factor", OxObjectHandle{ nativeOptiXProgramHandle(0U) });
}

OxProgram OxScatteringRenderingPass::getScatteringProbabilityShader() const
{
    return getOxProgramFromDeclarationOffset(1U);
}

void OxScatteringRenderingPass::setScatteringProbabilityShader(OxProgram const& scattering_probability_shader)
{
    replaceProgramAtOffset(scattering_probability_shader, 1U);

    static_cast<OxProgram&>(
        static_cast<OxMaterial&>(m_surface_material_assembly.getMaterialByRayType(OxRayType::unknown))
        .getClosestHitShader())
        .declareVariable("scattering_factor", OxObjectHandle{ nativeOptiXProgramHandle(1U) });
    static_cast<OxMissShader&>(m_miss_shader_assembly.getMissShaderByRayType(OxRayType::unknown)).
        getProgram().declareVariable("scattering_factor", OxObjectHandle{ nativeOptiXProgramHandle(1U) });
}

OxProgram OxScatteringRenderingPass::getScatteringPhaseFunctionShader() const
{
    return getOxProgramFromDeclarationOffset(2U);
}

void OxScatteringRenderingPass::setScatteringPhaseFunctionShader(OxProgram const& scattering_phase_function_shader)
{
    replaceProgramAtOffset(scattering_phase_function_shader, 2U);

    static_cast<OxProgram&>(static_cast<OxMaterial&>(m_surface_material_assembly.getMaterialByRayType(OxRayType::unknown))
        .getClosestHitShader())
        .declareVariable("phase_function", OxObjectHandle{ nativeOptiXProgramHandle(2U) });
    static_cast<OxMissShader&>(m_miss_shader_assembly.getMissShaderByRayType(OxRayType::unknown)).
        getProgram().declareVariable("phase_function", OxObjectHandle{ nativeOptiXProgramHandle(2U) });
}

void OxScatteringRenderingPass::render() const
{
    applyMaterialAssemblyToSceneSection(targetSceneSection(), m_surface_material_assembly);
    targetSceneSection().rayGenerator().setMissShaderAssembly(m_miss_shader_assembly);

    targetSceneSection().update();
    targetSceneSection().trace();

    unsigned int num_not_converged_rays{ *m_traverse_backup_buffer.getBufferPointer() };
    while (num_not_converged_rays > 0)
    {
        m_recaster_ray_generator.launch();
        num_not_converged_rays = *m_traverse_backup_buffer.getBufferPointer();
    }
}
