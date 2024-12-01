#include "EnergyLossCalculator.h"
#include <glm/gtc/random.hpp>

void EnergyLossCalculator::Init()
{
	if (m_Initialized)
		Destroy();

	m_Initialized = true;
}

void EnergyLossCalculator::Destroy()
{
	Reset();
	m_Initialized = false;
}

void EnergyLossCalculator::Reset()
{

}

void EnergyLossCalculator::PoolFunctionReflect(std::vector<float>* table, uint32_t tableIndex, uint32_t sampleCount, double roughness, double viewCosine, double anisotropy)
{
	double value = EnergyLossCalculator::AccumulateReflection(sampleCount, roughness, viewCosine, anisotropy);

	(*table)[tableIndex] = value;
}

void EnergyLossCalculator::PoolFunctionRefract(std::vector<float>* table, uint32_t tableIndex, uint32_t sampleCount, double roughness, double viewCosine, double IOR, bool AboveTheSurface)
{
	double value = EnergyLossCalculator::AccumulateRefraction(sampleCount, roughness, viewCosine, IOR, AboveTheSurface);

	(*table)[tableIndex] = value;
}

std::vector<float> EnergyLossCalculator::CalculateReflectionEnergyLoss(glm::dvec3 tableSize, uint32_t sampleCount, uint32_t coresCount)
{
	VulkanHelper::Timer timer;

	uint32_t totalTableSize = tableSize.z * tableSize.y * tableSize.x;
	uint64_t totalSampleCount = totalTableSize * sampleCount;
	VL_INFO("Calculating Reflection Table, Total Sample Count = {}", totalSampleCount);

	std::vector<float> table(totalTableSize);

	if (coresCount == 0)
		coresCount = std::thread::hardware_concurrency(); // Use all cores by default

	VulkanHelper::ThreadPool threadPool({ coresCount });

	// Compute using threadpool
	for (int a = 0; a < tableSize.z; a++)
	{
		for (int r = 0; r < tableSize.y; r++)
		{
			for (int v = 0; v < tableSize.x; v++)
			{
				uint32_t index = v + r * tableSize.x + a * tableSize.x * tableSize.y;

				double viewCosine = glm::clamp((double(v)) / double(tableSize.x), 0.0001, 0.9999);
				double roughness = glm::clamp((double(r)) / double(tableSize.y), 0.0001, 1.0);
				double anisotropy = double(a) / double(tableSize.z);

				threadPool.PushTask(PoolFunctionReflect, &table, index, sampleCount, roughness, viewCosine, anisotropy);
			}
		}
	}

	VulkanHelper::Timer timer1;
	while (true)
	{
		using namespace std::chrono_literals;
		std::this_thread::sleep_for(500ms);

		uint32_t tasksLeft = threadPool.TasksLeft();
		if (timer1.ElapsedMillis() > 2000)
		{
			VL_INFO("{:.1f}% calculated", float(totalTableSize - tasksLeft) / float(totalTableSize) * 100.0f);
			timer1.Reset();
		}

		if (tasksLeft == 0)
		{
			break;
		}
	}

	threadPool.Destroy();
	
	VL_INFO("Calculating Reflection Table Values Took: {}s", timer.ElapsedSeconds());

	return table;
}

std::vector<float> EnergyLossCalculator::CalculateRefractionEnergyLoss(glm::dvec3 tableSize, uint32_t sampleCount, bool AboveTheSurface, uint32_t coresCount /*= 0*/)
{
	VulkanHelper::Timer timer;

	uint32_t totalTableSize = tableSize.z * tableSize.y * tableSize.x;
	uint64_t totalSampleCount = totalTableSize * sampleCount;
	VL_INFO("Calculating Refraction Table, Total Sample Count = {}", totalSampleCount);

	std::vector<float> table(totalTableSize);

	if (coresCount == 0)
		coresCount = std::thread::hardware_concurrency(); // Use all cores by default

	VulkanHelper::ThreadPool threadPool({ coresCount });

	// Compute using threadpool
	for (int i = 0; i < tableSize.z; i++)
	{
		for (int r = 0; r < tableSize.y; r++)
		{
			for (int v = 0; v < tableSize.x; v++)
			{
				uint32_t index = v + r * tableSize.x + i * tableSize.x * tableSize.y;

				double viewCosine = glm::clamp(glm::pow((double(v)) / double(tableSize.x), 2.5), 0.0001, 0.9999);
				double roughness = glm::clamp((double(r)) / double(tableSize.y), 0.0001, 1.0);
				double IOR = 1.0 + glm::clamp(glm::pow((double(i)) / double(tableSize.y), 4.0) * 2.0, 0.0001, 3.0);

				threadPool.PushTask(PoolFunctionRefract, &table, index, sampleCount, roughness, viewCosine, IOR, AboveTheSurface);
			}
		}
	}

	VulkanHelper::Timer timer1;
	while (true)
	{
		using namespace std::chrono_literals;
		std::this_thread::sleep_for(500ms);

		uint32_t tasksLeft = threadPool.TasksLeft();
		if (timer1.ElapsedMillis() > 2000)
		{
			VL_INFO("{:.1f}% calculated", float(totalTableSize - tasksLeft) / float(totalTableSize) * 100.0f);
			timer1.Reset();
		}

		if (tasksLeft == 0)
		{
			break;
		}
	}

	threadPool.Destroy();

	VL_INFO("Calculating Refraction Table Values Took: {}s", timer.ElapsedSeconds());

	return table;
}

double EnergyLossCalculator::Lambda(glm::dvec3 V, double ax, double ay)
{
	double Vx2 = V.x * V.x;
	double Vy2 = V.y * V.y;
	double Vz2 = V.z * V.z;

	double ax2 = ax * ax;
	double ay2 = ay * ay;

	double nominator = -1.0 + glm::sqrt(1.0 + (ax2 * Vx2 + ay2 * Vy2) / Vz2);

	return nominator / 2.0;
}

double EnergyLossCalculator::GGXSmithAnisotropic(glm::dvec3 V, double ax, double ay)
{
	return 1.0 / (1.0 + Lambda(V, ax, ay));
}

glm::dvec3 EnergyLossCalculator::GGXSampleAnisotopic(glm::dvec3 Ve, double ax, double ay, double u1, double u2)
{
	glm::dvec3 Vh = glm::normalize(glm::dvec3(ax * Ve.x, ay * Ve.y, Ve.z));

	double lensq = Vh.x * Vh.x + Vh.y * Vh.y;
	glm::dvec3 T1 = lensq > 0 ? glm::dvec3(-Vh.y, Vh.x, 0) * glm::inversesqrt(lensq) : glm::dvec3(1, 0, 0);
	glm::dvec3 T2 = glm::cross(Vh, T1);

	double r = glm::sqrt(u1);
	double phi = 2.0 * glm::pi<double>() * u2;
	double t1 = r * glm::cos(phi);
	double t2 = r * glm::sin(phi);
	double s = 0.5 * (1.0 + Vh.z);
	t2 = (1.0 - s) * glm::sqrt(1.0 - t1 * t1) + s * t2;

	glm::dvec3 Nh = t1 * T1 + t2 * T2 + glm::sqrt(glm::max(0.0, 1.0 - t1 * t1 - t2 * t2)) * Vh;

	glm::dvec3 Ne = normalize(glm::dvec3(ax * Nh.x, ay * Nh.y, glm::max(0.0, Nh.z)));

	return Ne;
}

double EnergyLossCalculator::GGXDistributionAnisotropic(glm::dvec3 H, double ax, double ay)
{
	double Hx2 = H.x * H.x;
	double Hy2 = H.y * H.y;
	double Hz2 = H.z * H.z;

	double ax2 = ax * ax;
	double ay2 = ay * ay;

	return 1.0 / (double(M_PI) * ax * ay * glm::pow(Hx2 / ax2 + Hy2 / ay2 + Hz2, 2.0));
}

double EnergyLossCalculator::EvalReflection(glm::dvec3 L, glm::dvec3 V, glm::dvec3 H, double F, double ax, double ay)
{
	// BRDF = D * F * GV * GL / (4.0 * NdotV * NdotL) * NdotL
	// 
	// PDF is VNDF / jacobian of reflect()
	// PDF = (GV * VdotH * D / NdotV) / (4.0 * VdotH)
	//
	// Fr = BRDF / PDF
	//
	// If we expand it we get
	// 
	//      D * F * GV * GL * 4.0 * NdotV * NdotL * VdotH
	// Fr = ----------------------------------------------
	//          4.0 * NdotL * VdotH * NdotV * GV * D
	//
	// almost everything cancels out and we're only left with F * GL.

	double LdotH = glm::max(0.0, glm::dot(L, H));
	double VdotH = glm::max(0.0, glm::dot(V, H));

	double D = GGXDistributionAnisotropic(H, ax, ay);

	double GV = GGXSmithAnisotropic(V, ax, ay);
	double GL = GGXSmithAnisotropic(L, ax, ay);
	double G = GV * GL;

	//pdf = 1.0;
	//dvec3 bsdf = F * GL;

	double pdf = (GV * D) / (4.0 * V.z);
	double bsdf = D * F * GV * GL / (4.0 * V.z);

	pdf *= F;
	return bsdf / pdf;
}

double EnergyLossCalculator::SampleReflection(glm::dvec3 V, double F, double ax, double ay)
{
	glm::dvec3 H = GGXSampleAnisotopic(V, ax, ay, glm::linearRand(0.0, 1.0), glm::linearRand(0.0, 1.0));

	glm::dvec3 L = glm::normalize(glm::reflect(-V, H));

	if (L.z < 0.0)
		return 0.0;

	return EvalReflection(L, V, H, F, ax, ay);
}

double EnergyLossCalculator::AccumulateReflection(uint32_t sampleCount, double roughness, double viewCosine, double anisotropy)
{
	double ax, ay;
	const double aspect = glm::sqrt(1.0 - glm::sqrt(anisotropy) * 0.9);
	ax = glm::max(0.001, roughness / aspect);
	ay = glm::max(0.001, roughness * aspect);

	double totalEnergy = 0.0;

	for (int i = 0; i < int(sampleCount); i++)
	{
		// Generate random view dir
		double xyMagnitudeSquared = 1.0 - viewCosine * viewCosine;
		double phiV = glm::linearRand(0.0, glm::two_pi<double>());
		double x = glm::sqrt(xyMagnitudeSquared) * glm::cos(phiV);
		double y = glm::sqrt(xyMagnitudeSquared) * glm::sin(phiV);

		// leave z as viewCosine
		double z = viewCosine;

		glm::dvec3 V(x, y, z);
		V = glm::normalize(V);

		double brdf = SampleReflection(V, 1.0, ax, ay);

		totalEnergy += brdf;
	}

	return totalEnergy / sampleCount;
}

double EnergyLossCalculator::EvalRefraction(double ax, double ay, double eta, glm::dvec3 L, glm::dvec3 V, glm::dvec3 H, double F)
{
	double VdotH = abs(dot(V, H));
	double LdotH = abs(dot(L, H));

	double D = GGXDistributionAnisotropic(H, ax, ay);
	double GV = GGXSmithAnisotropic(V, ax, ay);
	double GL = GGXSmithAnisotropic(L, ax, ay);
	double G = GV * GL;

	double denominator = (LdotH + eta * VdotH);
	double denominator2 = denominator * denominator;
	double eta2 = eta * eta;

	double jacobian = (eta2 * LdotH) / denominator2;

	double pdf = (GV * VdotH * D / V.z) * jacobian;
	double bsdf = ((1.0 - F) * D * G * eta2 / denominator2) * (VdotH * LdotH / abs(V.z));

	pdf *= (1.0 - F);

	return bsdf / pdf;
}

double EnergyLossCalculator::DielectricFresnel(double VdotH, double eta)
{
	double cosThetaI = VdotH;
	double sinThetaTSq = eta * eta * (1.0 - cosThetaI * cosThetaI);

	// Total internal reflection
	if (sinThetaTSq > 1.0)
		return 1.0;

	double cosThetaT = glm::sqrt(glm::max(1.0 - sinThetaTSq, 0.0));

	double rs = (eta * cosThetaT - cosThetaI) / (eta * cosThetaT + cosThetaI);
	double rp = (eta * cosThetaI - cosThetaT) / (eta * cosThetaI + cosThetaT);

	return 0.5 * (rs * rs + rp * rp);
}

double EnergyLossCalculator::SampleRefraction(glm::dvec3 V, double ax, double ay, double IOR, bool AboveTheSurface)
{
	V = glm::normalize(V);
	glm::dvec3 H = GGXSampleAnisotopic(V, ax, ay, glm::linearRand(0.0, 1.0), glm::linearRand(0.0, 1.0));

	double rand = glm::linearRand(0.0, 1.0);
	double eta;

	if (AboveTheSurface)
		eta = IOR;
	else
		eta = (1.0 / IOR);

	H = glm::normalize(H);
	glm::dvec3 L = glm::normalize(glm::refract(-V, H, eta));
	if (glm::isnan(L.x) || glm::isnan(L.y) || glm::isnan(L.z))
		return 0.0f;

	bool reflect = L.z > 0.0;

	double F = DielectricFresnel(glm::abs(glm::dot(V, H)), eta);

	double bsdf;
	if (reflect)
		bsdf = EvalReflection(L, V, H, F, ax, ay);
	else
		bsdf = EvalRefraction(ax, ay, eta, L, V, H, (1.0f - F));

	return bsdf;
}

double EnergyLossCalculator::AccumulateRefraction(uint32_t sampleCount, double roughness, double viewCosine, double ior, bool AboveTheSurface)
{
	double ax = roughness;
	double ay = roughness;

	double totalEnergy = 0.0;

	srand(time(0));

	for (int i = 0; i < sampleCount; i++)
	{
		// Generate random view dir
		double xyMagnitudeSquared = 1.0 - viewCosine * viewCosine;
		double phiV = glm::linearRand(0.0, glm::two_pi<double>());
		double x = glm::sqrt(xyMagnitudeSquared) * glm::cos(phiV);
		double y = glm::sqrt(xyMagnitudeSquared) * glm::sin(phiV);

		// leave z as viewCosine
		double z = viewCosine;

		glm::dvec3 V(x, y, z);
		V = glm::normalize(V);

		double bsdf = SampleRefraction(V, ax, ay, ior, AboveTheSurface);

		if (glm::isnan(bsdf) || glm::isinf(bsdf) || bsdf != bsdf)
			totalEnergy += 0.0;
		else
			totalEnergy += bsdf;
	}

	return totalEnergy / sampleCount;
}