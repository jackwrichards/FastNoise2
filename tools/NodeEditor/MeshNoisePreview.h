#pragma once
#include <unordered_set>
#include <unordered_map>
#include <vector>
#include <chrono>
#include <cstring>
#include <thread>

#include <Magnum/GL/Buffer.h>
#include <Magnum/GL/Mesh.h>
#include <Magnum/Math/Vector3.h>
#include <Magnum/Math/Color.h>
#include <Magnum/GL/AbstractShaderProgram.h>
#include <Magnum/GL/Shader.h>
#include <Magnum/Shaders/FlatGL.h>

#include <robin_hood.h>

#include "FastNoise/FastNoise.h"
#include "MultiThreadQueues.h"

namespace Magnum
{
    class MeshNoisePreview
    {
    public:
        MeshNoisePreview();
        ~MeshNoisePreview();

        void ReGenerate( FastNoise::SmartNodeArg<> generator );

        void Draw( const Matrix4& transformation, const Matrix4& projection, const Vector3& cameraPosition );

        struct ColorLayer
        {
            Color3 color;
            float threshold;
        };

        const std::vector<ColorLayer>& GetColorLayers() const { return mColorLayers; }
        float GetLayerSmoothness() const { return mLayerSmoothness; }
        float GetHeightmapMultiplier() const { return mBuildData.heightmapMultiplier; }

        bool GetOrbitMode() const { return mOrbitMode; }
        float GetFovDeg() const { return mFovDeg; }
        float GetOrbitDistance() const { return mOrbitDistance; }
        void SetOrbitDistance( float d ) { mOrbitDistance = std::clamp( d, 5.0f, 5000.0f ); }
        float GetOrbitHeight() const { return mOrbitHeight; }
        void SetOrbitHeight( float h ) { mOrbitHeight = std::clamp( h, -5000.0f, 5000.0f ); }

    private:
        enum MeshType
        {
            MeshType_Bloxel3D,
            MeshType_DualMarchingCubes3D,
            MeshType_Heightmap2D,
            MeshType_Count
        };

        inline static const char* MeshTypeStrings =
            "Bloxel 3D\0"
            "Dual Marching Cubes 3D\0"
            "Heightmap 2D\0";

        enum UpAxis
        {
            UpAxis_Y,
            UpAxis_Z,
            UpAxis_Count
        };

        inline static const char* UpAxisStrings =
            "Y Up\0"
            "Z Up\0";

        class VertexLightShader : public GL::AbstractShaderProgram
        {
        public:
            typedef GL::Attribute<0, Vector4> PositionLight;

            explicit VertexLightShader();
            explicit VertexLightShader( NoCreateT ) noexcept : AbstractShaderProgram{ NoCreate } {}

            VertexLightShader( const VertexLightShader& ) = delete;
            VertexLightShader( VertexLightShader&& ) noexcept = default;
            VertexLightShader& operator=( const VertexLightShader& ) = delete;
            VertexLightShader& operator=( VertexLightShader&& ) noexcept = default;

            static constexpr int MAX_COLOR_LAYERS = 8;

            VertexLightShader& SetTransformationProjectionMatrix( const Matrix4& matrix );
            VertexLightShader& SetColorLayers( const Color3* colors, const float* thresholds, int count, float smoothness );

        private:
            GL::Shader CreateShader( GL::Version version, GL::Shader::Type type );

            int mTransformationProjectionMatrixUniform = 0;
            int mLayerColorsUniform = 1;
            int mLayerThresholdsUniform = 9;
            int mLayerCountUniform = 17;
            int mLayerSmoothnessUniform = 18;
        };

        class Chunk
        {
        public:
            struct VertexData
            {
                VertexData( Vector3 p, float c ) :
                    posLight( p, c ) {}

                Vector4 posLight;
            };

            struct MeshData
            {
                MeshData() = default;

                MeshData( Vector3i p, FastNoise::OutputMinMax mm,
                          const std::vector<VertexData>& v, const std::vector<uint32_t>& i,
                          float minAir = INFINITY, float maxSolid = -INFINITY ) :
                          pos( p ), minMax( mm ), minAirY( minAir ), maxSolidY( maxSolid )
                {
                    if( v.empty() )
                    {
                        return;
                    }

                    size_t vertSize = sizeof( VertexData ) * v.size();
                    size_t indSize = sizeof( uint32_t ) * i.size();

                    void* vertexDataPtr = std::malloc( vertSize + indSize );
                    void* indiciesPtr = (uint8_t*)vertexDataPtr + vertSize;

                    std::memcpy( vertexDataPtr, v.data(), vertSize );
                    std::memcpy( indiciesPtr, i.data(), indSize );

                    vertexData = { (VertexData*)vertexDataPtr, v.size() };
                    indicies = { (uint32_t*)indiciesPtr, i.size() };
                }

                void Free()
                {
                    std::free( vertexData.data() );

                    vertexData = nullptr;
                    indicies = nullptr;
                }

                Vector3i pos;
                Containers::ArrayView<VertexData> vertexData;
                Containers::ArrayView<uint32_t> indicies;
                FastNoise::OutputMinMax minMax;
                float minAirY, maxSolidY;
            };

            struct BuildData
            {
                FastNoise::SmartNode<const FastNoise::Generator> generator;
                FastNoise::SmartNode<FastNoise::DomainScale> generatorScaled;
                Vector3i pos;
                float scale, isoSurface, heightmapMultiplier;
                int32_t seed;
                MeshType meshType;
                UpAxis upAxis;
                uint32_t genVersion;
            };

            static MeshData BuildMeshData( const BuildData& buildData );
            static MeshData BuildBloxel3DMesh( const BuildData& buildData, float* densityValues, std::vector<VertexData>& vertexData, std::vector<uint32_t>& indicies );
            static MeshData BuildDmc3DMesh( const BuildData& buildData, float* densityValues, std::vector<VertexData>& vertexData, std::vector<uint32_t>& indicies );
            static MeshData BuildHeightMap2DMesh( const BuildData& buildData, float* densityValues, std::vector<VertexData>& vertexData, std::vector<uint32_t>& indicies );

            Chunk( MeshData& meshData );

            GL::Mesh* GetMesh() { return mMesh.get(); }
            Vector3i GetPos() const { return mPos; }

            static constexpr uint32_t SIZE           = 128;
            static constexpr Vector3  LIGHT_DIR      = { 0.6f, 1.f, 0.4f };
            static constexpr float    AMBIENT_LIGHT  = 0.3f;
            static constexpr float    AO_STRENGTH    = 0.9f;

        private:
            static void BloxelAddQuadAO( std::vector<VertexData>& verts, std::vector<uint32_t>& indicies, const float* density, float isoSurface,
                                         int32_t idx, int32_t facingIdx, int32_t offsetA, int32_t offsetB, float light, Vector3 pos00, Vector3 pos01, Vector3 pos11, Vector3 pos10 );

            template<uint32_t STEP_X, uint32_t STEP_Y, uint32_t STEP_Z, uint32_t SIZE_GEN>
            static uint32_t DmcGetVertIndex( uint32_t cellIndex, uint16_t edge, Vector3 vertOffset, const float* densityArray, float isoSurface,
                std::vector<VertexData>& vertexData, robin_hood::unordered_flat_map<uint64_t, uint32_t>& vertIndexMap );

            static constexpr uint32_t SIZE_DENSITY = SIZE + 4;

            Vector3i mPos;
            std::unique_ptr<GL::Mesh> mMesh;
        };

        struct Vector3iHash
        {
            size_t operator()( const Vector3i& v ) const
            {
                return robin_hood::hash<size_t>()( 
                    (size_t)v.x() ^ 
                    ((size_t)v.y() << sizeof( size_t ) * 2) ^ 
                    ((size_t)v.z() << sizeof( size_t ) * 4) );
            }
        };

        static void GenerateLoopThread( GenerateQueue<Chunk::BuildData>& generateQueue, CompleteQueue<Chunk::MeshData>& completeQueue );

        void UploadColorLayers();

        void UpdateChunksForPosition( Vector3 position );
        void UpdateChunkQueues( const Vector3& position );
        float GetLoadRangeModifier();

        void StartTimer();
        float GetTimerDurationMs();
        void SetupSettingsHandlers();
        
        robin_hood::unordered_set<Vector3i, Vector3iHash> mRegisteredChunkPositions;
        std::vector<Chunk> mChunks;

        bool mEnabled = true;
        UpAxis mUpAxis = UpAxis_Y;
        Chunk::BuildData mBuildData;
        float mLoadRange = 300.0f;
        float mAvgNewChunks = 1.0f;
        uint32_t mTriLimit = 35000000; // 35 mil
        uint32_t mTriCount = 0;
        uint32_t mMeshesCount = 0;
        int mStaggerCheck = 0;

        FastNoise::OutputMinMax mMinMax;
        float mMinAirY, mMaxSolidY;

        GenerateQueue<Chunk::BuildData> mGenerateQueue;
        CompleteQueue<Chunk::MeshData> mCompleteQueue;
        std::vector<std::thread> mThreads;
        std::chrono::high_resolution_clock::time_point mTimerStart;

        VertexLightShader mShader;

        std::vector<ColorLayer> mColorLayers;
        float mLayerSmoothness = 0.0f;

        enum BorderShape
        {
            BorderShape_None,
            BorderShape_Circle,
            BorderShape_Box,
            BorderShape_Count
        };

        inline static const char* BorderShapeStrings =
            "None\0"
            "Circle\0"
            "Box\0";

        void RebuildBorderMesh();
        void DrawBorder( const Matrix4& transformationProjection );
        bool IsChunkInsideBorder( const Vector3i& chunkPos ) const;

        BorderShape mBorderShape = BorderShape_None;
        bool mGenerateOutsideBorder = true;
        float mBorderRadius = 256.0f;
        float mBorderMinY = 0.0f;
        float mBorderMaxY = 100.0f;
        int mBorderRings = 4;
        int mBorderSegments = 64;
        Color3 mBorderColor = Color3( 1.0f, 0.85f, 0.2f );
        bool mBorderDirty = true;

        Shaders::FlatGL3D mBorderShader { NoCreate };
        GL::Buffer mBorderBuffer { NoCreate };
        GL::Mesh mBorderMesh { NoCreate };
        int mBorderVertexCount = 0;

        bool mOrbitMode = false;
        float mOrbitDistance = 400.0f;
        float mOrbitHeight = 0.0f;
        float mFovDeg = 70.0f;
    };
}
