# Procedural (port z NAF)

W NAF folder **Animation/Procedural** zawiera m.in.:

- **PDataObject** – bazowa klasa obiektów danych (constraint/spring); w SAF dodane w tym folderze (kompatybilne z Physics).
- **PNode.h / PNode.cpp** – węzeł grafu proceduralnego (Evaluate, AdvanceTime, Synchronize), zależny od PoseCache, Ozz, Util::variant_index.
- **PGraph.h / PGraph.cpp** – graf proceduralny dziedziczący z IAnimationFile; zależny od PNode, PoseCache, IAnimationFile.
- **PFullAnimationNode**, **PBasePoseNode**, **PStaticPoseNode** – węzły pose.
- **PSpringBoneNode**, **PSpringPropsNode** – węzły spring.
- **PBlend1DNode**, **PBlend2DNode**, **PAdditiveBlendNode** – blendy.
- **PTwoBoneIKNode**, **POneBoneIKNode** – IK.
- **PLinearBoxConstrNode**, **PLinearSphereConstrNode**, **PAngularConeConstrNode** – constrainty.
- **PFixedValueNode**, **PSmoothValNode**, **PSmoothedRandNode**, **PLimitROCNode**, **PTransformRangeNode** – wartości/wektory.
- **PVariableNode**, **PInternalCacheReleaseNode**, **SimpleNodes.cpp** – zmienne i pomocnicze.

Pełny port wymaga w SAF:

- **PoseCache** (cache pozów ozz) – w NAF w Animation/.
- **IAnimationFile** (abstrakcja pliku animacji z CreateGenerator()) – w SAF na razie jest tylko Generator/ClipGenerator; można dodać interfejs.
- **Util::variant_index** (dla PEvaluationResult) – można dodać do Util/General.h lub zdefiniować w Procedural.
- Ewentualna integracja z GraphManager (ProceduralGenerator jak w NAF).

Pliki **PDataObject** są już dodane i używają Physics z SAF.
