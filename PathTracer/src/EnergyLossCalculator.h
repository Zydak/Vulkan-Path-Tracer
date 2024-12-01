#include "VulkanHelper.h"

class EnergyLossCalculator
{
public:
	EnergyLossCalculator() { Init(); }

	std::vector<float> CalculateReflectionEnergyLoss(glm::dvec3 tableSize, uint32_t sampleCount, uint32_t coresCount = 0);
	std::vector<float> CalculateRefractionEnergyLoss(glm::dvec3 tableSize, uint32_t sampleCount, bool AboveTheSurface, uint32_t coresCount = 0);

	void Init();
	void Destroy();
private:

	static double Lambda(glm::dvec3 V, double ax, double ay);
	static double GGXSmithAnisotropic(glm::dvec3 V, double ax, double ay);
	static glm::dvec3 GGXSampleAnisotopic(glm::dvec3 Ve, double ax, double ay, double u1, double u2);
	static double GGXDistributionAnisotropic(glm::dvec3 H, double ax, double ay);
	static double EvalReflection(glm::dvec3 L, glm::dvec3 V, glm::dvec3 H, double F, double ax, double ay);
	static double SampleReflection(glm::dvec3 V, double F, double ax, double ay);
	static double SampleRefraction(glm::dvec3 V, double ax, double ay, double IOR, bool AboveTheSurface);
	static double DielectricFresnel(double VdotH, double eta);
	static double EvalRefraction(double ax, double ay, double eta, glm::dvec3 L, glm::dvec3 V, glm::dvec3 H, double F);

	static double AccumulateRefraction(uint32_t sampleCount, double roughness, double viewCosine, double ior, bool AboveTheSurface);
	static double AccumulateReflection(uint32_t sampleCount, double roughness, double viewCosine, double anisotropy);

	static void PoolFunctionReflect(std::vector<float>* table, uint32_t tableIndex, uint32_t sampleCount, double roughness, double viewCosine, double anisotropy);
	static void PoolFunctionRefract(std::vector<float>* table, uint32_t tableIndex, uint32_t sampleCount, double roughness, double viewCosine, double IOR, bool AboveTheSurface);

	bool m_Initialized = false;
	void Reset();
};