// MathTest.cpp
// 测试数学接口功能
#include <unordered_map>
#include "TestBase.h"
#include "Math.hpp"
#include "Transform.hpp"
using namespace Core;
namespace Test {

    class MathTest : public TestBase {
    public:
        void Setup() override {

        }


        void Run() override {
            TestLookAt();
            TestPerspective();
            TestViewProjection();
        }

        void Teardown() override {

        }

    private:
        // --------------------------------------------
       // helper
       // --------------------------------------------

        static void PrintVec4(
            const std::array<float, 4>& v,
            const char* name)
        {
            std::cout
                << name
                << " = ("
                << v[0] << ", "
                << v[1] << ", "
                << v[2] << ", "
                << v[3] << ")\n";
        }

        static std::array<float, 4>
            MakeVec4(
                float x,
                float y,
                float z,
                float w)
        {
            return { x,y,z,w };
        }

        // --------------------------------------------
        // LookAt
        // --------------------------------------------

        void TestLookAt()
        {
            std::cout
                << "\n========== LookAt Test ==========\n";

            Float3 eye =
            {
                0.0f,
                0.0f,
                -5.0f
            };

            Float3 target =
            {
                0.0f,
                0.0f,
                0.0f
            };

            Float3 up =
            {
                0.0f,
                1.0f,
                0.0f
            };

            Mat4 view =
                LookAtRH(
                    eye,
                    target,
                    up);
            view.Print(std::cout);

            // world origin
            auto worldOrigin =
                MakeVec4(
                    0,
                    0,
                    0,
                    1);

            auto viewPos =
                view *
                worldOrigin;

            PrintVec4(
                viewPos,
                "Origin In View");

            std::cout
                << "Expected: z ≈ -5\n";
        }

        // --------------------------------------------
        // Projection
        // --------------------------------------------

        void TestPerspective()
        {
            std::cout
                << "\n========== Projection Test ==========\n";

            Mat4 proj =
                PerspectiveRhNo(
                    DegToRad(45.0f),
                    1.0f,
                    0.1f,
                    100.0f);
            proj.Print(std::cout);


            auto cameraForward =
                MakeVec4(
                    0,
                    0,
                    -5,
                    1);

            auto clip =
                proj *
                cameraForward;

            PrintVec4(
                clip,
                "Projected Point");

            std::cout
                << "Expected:\n";
            std::cout
                << "x≈0 y≈0\n";
            std::cout
                << "w>0\n";
        }

        // --------------------------------------------
        // VP
        // --------------------------------------------

        void TestViewProjection()
        {
            std::cout
                << "\n========== VP Test ==========\n";

            Float3 eye =
            {
                0.0f,
                0.0f,
                -5.0f
            };

            Float3 target =
            {
                0.0f,
                0.0f,
                0.0f
            };

            Float3 up =
            {
                0.0f,
                1.0f,
                0.0f
            };

            Mat4 view =
                LookAtRH(
                    eye,
                    target,
                    up);

            Mat4 proj =
                PerspectiveRhNo(
                    DegToRad(45.0f),
                    1.0f,
                    0.1f,
                    100.0f);

            Mat4 vp =
                proj *
                view;
            vp.Print(std::cout);

            // 原点
            auto worldOrigin =
                MakeVec4(
                    0,
                    0,
                    0,
                    1);

            auto clip =
                vp *
                worldOrigin;

            PrintVec4(
                clip,
                "Origin Clip");

            // NDC
            if (std::abs(clip[3]) > 1e-6f)
            {
                std::array<float, 3> ndc =
                {
                    clip[0] / clip[3],
                    clip[1] / clip[3],
                    clip[2] / clip[3]
                };

                std::cout
                    << "NDC = ("
                    << ndc[0]
                    << ", "
                    << ndc[1]
                    << ", "
                    << ndc[2]
                    << ")\n";

                std::cout
                    << "Expected:\n";
                std::cout
                    << "center => x≈0 y≈0\n";
            }
            else
            {
                std::cout
                    << "ERROR: w == 0\n";
            }

            // 前方一点
            auto frontPoint =
                MakeVec4(
                    0,
                    0,
                    1,
                    1);

            auto clip2 =
                vp *
                frontPoint;

            PrintVec4(
                clip2,
                "Front Point Clip");
        }
    };

    REGISTER_RENDER_TEST("MathTest", MathTest);
}