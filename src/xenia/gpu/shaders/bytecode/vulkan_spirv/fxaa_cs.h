// Generated with `xb buildshaders`.
#if 0
; SPIR-V
; Version: 1.0
; Generator: Khronos Glslang Reference Front End; 11
; Bound: 25074
; Schema: 0
               OpCapability Shader
               OpCapability StorageImageExtendedFormats
          %1 = OpExtInstImport "GLSL.std.450"
               OpMemoryModel Logical GLSL450
               OpEntryPoint GLCompute %main "main" %gl_GlobalInvocationID
               OpExecutionMode %main LocalSize 16 8 1
               OpSource GLSL 460
               OpSourceExtension "GL_GOOGLE_cpp_style_line_directive"
               OpSourceExtension "GL_GOOGLE_include_directive"
               OpName %main "main"
               OpName %gl_GlobalInvocationID "gl_GlobalInvocationID"
               OpName %FxaaConstants "FxaaConstants"
               OpMemberName %FxaaConstants 0 "xe_fxaa_size"
               OpMemberName %FxaaConstants 1 "xe_fxaa_size_inv"
               OpName %_ ""
               OpName %xe_fxaa_source "xe_fxaa_source"
               OpName %xe_fxaa_dest "xe_fxaa_dest"
               OpDecorate %gl_GlobalInvocationID BuiltIn GlobalInvocationId
               OpDecorate %FxaaConstants Block
               OpMemberDecorate %FxaaConstants 0 Offset 0
               OpMemberDecorate %FxaaConstants 1 Offset 8
               OpDecorate %xe_fxaa_source Binding 0
               OpDecorate %xe_fxaa_source DescriptorSet 1
               OpDecorate %xe_fxaa_dest NonReadable
               OpDecorate %xe_fxaa_dest Binding 0
               OpDecorate %xe_fxaa_dest DescriptorSet 0
               OpDecorate %gl_WorkGroupSize BuiltIn WorkgroupSize
       %void = OpTypeVoid
       %1282 = OpTypeFunction %void
      %float = OpTypeFloat 32
    %v4float = OpTypeVector %float 4
    %v2float = OpTypeVector %float 2
        %150 = OpTypeImage %float 2D 0 0 0 1 Unknown
        %510 = OpTypeSampledImage %150
%_ptr_UniformConstant_510 = OpTypePointer UniformConstant %510
       %uint = OpTypeInt 32 0
     %uint_0 = OpConstant %uint 0
     %uint_1 = OpConstant %uint 1
    %float_0 = OpConstant %float 0
        %int = OpTypeInt 32 1
      %int_3 = OpConstant %int 3
      %v2int = OpTypeVector %int 2
     %int_n1 = OpConstant %int -1
       %1794 = OpConstantComposite %v2int %int_n1 %int_n1
       %bool = OpTypeBool
      %int_1 = OpConstant %int 1
       %1812 = OpConstantComposite %v2int %int_1 %int_n1
       %1818 = OpConstantComposite %v2int %int_n1 %int_1
    %float_1 = OpConstant %float 1
   %float_n2 = OpConstant %float -2
    %float_2 = OpConstant %float 2
%float_0_0833333358 = OpConstant %float 0.0833333358
  %float_0_5 = OpConstant %float 0.5
    %float_3 = OpConstant %float 3
    %float_4 = OpConstant %float 4
  %float_1_5 = OpConstant %float 1.5
   %float_12 = OpConstant %float 12
     %v2uint = OpTypeVector %uint 2
     %v3uint = OpTypeVector %uint 3
%_ptr_Input_v3uint = OpTypePointer Input %v3uint
%gl_GlobalInvocationID = OpVariable %_ptr_Input_v3uint Input
%FxaaConstants = OpTypeStruct %v2uint %v2float
%_ptr_PushConstant_FxaaConstants = OpTypePointer PushConstant %FxaaConstants
          %_ = OpVariable %_ptr_PushConstant_FxaaConstants PushConstant
      %int_0 = OpConstant %int 0
%_ptr_PushConstant_v2uint = OpTypePointer PushConstant %v2uint
     %v2bool = OpTypeVector %bool 2
%_ptr_PushConstant_v2float = OpTypePointer PushConstant %v2float
%xe_fxaa_source = OpVariable %_ptr_UniformConstant_510 UniformConstant
 %float_0_75 = OpConstant %float 0.75
%float_0_165999994 = OpConstant %float 0.165999994
%float_0_0833000019 = OpConstant %float 0.0833000019
        %166 = OpTypeImage %float 2D 0 0 0 2 Rgb10A2
%_ptr_UniformConstant_166 = OpTypePointer UniformConstant %166
%xe_fxaa_dest = OpVariable %_ptr_UniformConstant_166 UniformConstant
    %uint_16 = OpConstant %uint 16
     %uint_8 = OpConstant %uint 8
%gl_WorkGroupSize = OpConstantComposite %v3uint %uint_16 %uint_8 %uint_1
       %1566 = OpConstantComposite %v2float %float_0_5 %float_0_5
 %float_0_25 = OpConstant %float 0.25
   %float_n1 = OpConstant %float -1
          %2 = OpUndef %v4float
       %main = OpFunction %void None %1282
      %15110 = OpLabel
               OpSelectionMerge %21573 None
               OpSwitch %uint_0 %12914
      %12914 = OpLabel
      %13761 = OpLoad %v3uint %gl_GlobalInvocationID
      %21717 = OpVectorShuffle %v2uint %13761 %13761 0 1
       %7760 = OpAccessChain %_ptr_PushConstant_v2uint %_ %int_0
      %13378 = OpLoad %v2uint %7760
      %23437 = OpUGreaterThanEqual %v2bool %21717 %13378
      %23076 = OpAny %bool %23437
               OpSelectionMerge %9574 None
               OpBranchConditional %23076 %21992 %9574
      %21992 = OpLabel
               OpBranch %21573
       %9574 = OpLabel
      %17831 = OpConvertUToF %v2float %21717
      %24187 = OpFAdd %v2float %17831 %1566
      %11863 = OpAccessChain %_ptr_PushConstant_v2float %_ %int_1
      %23821 = OpLoad %v2float %11863
      %19130 = OpFMul %v2float %24187 %23821
               OpSelectionMerge %16763 None
               OpSwitch %uint_0 %9492
       %9492 = OpLabel
      %14661 = OpCompositeExtract %float %19130 0
      %13870 = OpCompositeExtract %float %19130 1
      %17361 = OpLoad %510 %xe_fxaa_source
      %23004 = OpImageSampleExplicitLod %v4float %17361 %19130 Lod %float_0
      %17175 = OpLoad %510 %xe_fxaa_source
      %17090 = OpImageGather %v4float %17175 %19130 %int_3
       %9106 = OpLoad %510 %xe_fxaa_source
      %13340 = OpImageGather %v4float %9106 %19130 %int_3 ConstOffset %1794
      %11752 = OpCompositeExtract %float %17090 0
      %23382 = OpCompositeExtract %float %23004 3
      %10966 = OpExtInst %float %1 FMax %11752 %23382
      %17822 = OpExtInst %float %1 FMin %11752 %23382
       %6803 = OpCompositeExtract %float %17090 2
      %20834 = OpExtInst %float %1 FMax %6803 %10966
      %19093 = OpExtInst %float %1 FMin %6803 %17822
      %11293 = OpCompositeExtract %float %13340 2
      %24381 = OpCompositeExtract %float %13340 0
       %9695 = OpExtInst %float %1 FMax %11293 %24381
      %13332 = OpExtInst %float %1 FMin %11293 %24381
      %16858 = OpExtInst %float %1 FMax %9695 %20834
      %20915 = OpExtInst %float %1 FMin %13332 %19093
       %8783 = OpFMul %float %16858 %float_0_165999994
      %10762 = OpFSub %float %16858 %20915
      %17273 = OpExtInst %float %1 FMax %float_0_0833000019 %8783
      %20129 = OpFOrdLessThan %bool %10762 %17273
               OpSelectionMerge %17618 None
               OpBranchConditional %20129 %21993 %17618
      %21993 = OpLabel
               OpBranch %16763
      %17618 = OpLabel
      %10895 = OpLoad %510 %xe_fxaa_source
      %21496 = OpImageSampleExplicitLod %v4float %10895 %19130 Lod|ConstOffset %float_0 %1812
      %12451 = OpCompositeExtract %float %21496 3
      %20158 = OpLoad %510 %xe_fxaa_source
      %20171 = OpImageSampleExplicitLod %v4float %20158 %19130 Lod|ConstOffset %float_0 %1818
      %20537 = OpCompositeExtract %float %20171 3
      %22659 = OpFAdd %float %11293 %11752
      %18660 = OpFAdd %float %24381 %6803
       %7165 = OpFDiv %float %float_1 %10762
      %10949 = OpFAdd %float %22659 %18660
      %15014 = OpFMul %float %float_n2 %23382
      %17933 = OpFAdd %float %15014 %22659
      %12300 = OpFAdd %float %15014 %18660
      %18159 = OpCompositeExtract %float %17090 1
      %15769 = OpFAdd %float %12451 %18159
      %12720 = OpCompositeExtract %float %13340 3
       %6773 = OpFAdd %float %12720 %12451
       %6246 = OpFMul %float %float_n2 %6803
      %18153 = OpFAdd %float %6246 %15769
       %9366 = OpFMul %float %float_n2 %11293
      %18845 = OpFAdd %float %9366 %6773
      %20848 = OpFAdd %float %12720 %20537
      %14459 = OpFAdd %float %20537 %18159
      %23097 = OpExtInst %float %1 FAbs %17933
      %21232 = OpFMul %float %23097 %float_2
      %23225 = OpExtInst %float %1 FAbs %18153
      %19164 = OpFAdd %float %21232 %23225
      %10915 = OpExtInst %float %1 FAbs %12300
      %21233 = OpFMul %float %10915 %float_2
       %6449 = OpExtInst %float %1 FAbs %18845
      %11240 = OpFAdd %float %21233 %6449
      %19562 = OpFMul %float %float_n2 %24381
      %18154 = OpFAdd %float %19562 %20848
       %7147 = OpFMul %float %float_n2 %11752
       %6930 = OpFAdd %float %7147 %14459
       %7944 = OpExtInst %float %1 FAbs %18154
      %15862 = OpFAdd %float %7944 %19164
      %13826 = OpExtInst %float %1 FAbs %6930
       %7794 = OpFAdd %float %13826 %11240
      %23579 = OpFAdd %float %20848 %15769
       %8594 = OpCompositeExtract %float %23821 0
      %21917 = OpFOrdGreaterThanEqual %bool %15862 %7794
      %15750 = OpFMul %float %10949 %float_2
      %21566 = OpFAdd %float %15750 %23579
       %9632 = OpLogicalNot %bool %21917
               OpSelectionMerge %21910 None
               OpBranchConditional %9632 %12760 %21910
      %12760 = OpLabel
      %21521 = OpCompositeInsert %v4float %24381 %2 2
               OpBranch %21910
      %21910 = OpLabel
      %10924 = OpPhi %v4float %13340 %17618 %21521 %12760
               OpSelectionMerge %21911 None
               OpBranchConditional %9632 %12761 %21911
      %12761 = OpLabel
      %21522 = OpCompositeInsert %v4float %6803 %2 0
               OpBranch %21911
      %21911 = OpLabel
      %10925 = OpPhi %v4float %17090 %21910 %21522 %12761
               OpSelectionMerge %15709 None
               OpBranchConditional %21917 %12933 %15709
      %12933 = OpLabel
      %10643 = OpCompositeExtract %float %23821 1
               OpBranch %15709
      %15709 = OpLabel
       %9314 = OpPhi %float %8594 %21911 %10643 %12933
      %20544 = OpFMul %float %21566 %float_0_0833333358
       %7800 = OpFSub %float %20544 %23382
      %14084 = OpCompositeExtract %float %10924 2
      %20105 = OpFSub %float %14084 %23382
      %11800 = OpCompositeExtract %float %10925 0
      %11763 = OpFSub %float %11800 %23382
      %10697 = OpFAdd %float %14084 %23382
      %13777 = OpFAdd %float %11800 %23382
      %15252 = OpExtInst %float %1 FAbs %20105
      %21208 = OpExtInst %float %1 FAbs %11763
      %17727 = OpFOrdGreaterThanEqual %bool %15252 %21208
       %6817 = OpExtInst %float %1 FMax %15252 %21208
               OpSelectionMerge %24685 None
               OpBranchConditional %17727 %24046 %24685
      %24046 = OpLabel
      %15185 = OpFNegate %float %9314
               OpBranch %24685
      %24685 = OpLabel
      %17200 = OpPhi %float %9314 %15709 %15185 %24046
      %19727 = OpExtInst %float %1 FAbs %7800
      %10606 = OpFMul %float %19727 %7165
      %17353 = OpExtInst %float %1 FClamp %10606 %float_0 %float_1
      %10824 = OpSelect %float %9632 %float_0 %8594
               OpSelectionMerge %17875 None
               OpBranchConditional %21917 %21994 %17217
      %21994 = OpLabel
               OpBranch %17875
      %17217 = OpLabel
      %13065 = OpCompositeExtract %float %23821 1
               OpBranch %17875
      %17875 = OpLabel
      %10926 = OpPhi %float %float_0 %21994 %13065 %17217
               OpSelectionMerge %21912 None
               OpBranchConditional %9632 %20728 %21912
      %20728 = OpLabel
      %21775 = OpFMul %float %17200 %float_0_5
       %6375 = OpFAdd %float %14661 %21775
      %14676 = OpCompositeInsert %v2float %6375 %19130 0
               OpBranch %21912
      %21912 = OpLabel
      %10927 = OpPhi %v2float %19130 %17875 %14676 %20728
               OpSelectionMerge %18756 None
               OpBranchConditional %21917 %19816 %18756
      %19816 = OpLabel
      %11901 = OpFMul %float %17200 %float_0_5
      %13372 = OpCompositeExtract %float %10927 1
      %21102 = OpFAdd %float %13372 %11901
      %24094 = OpCompositeInsert %v2float %21102 %10927 1
               OpBranch %18756
      %18756 = OpLabel
      %18310 = OpPhi %v2float %10927 %21912 %24094 %19816
      %14404 = OpCompositeExtract %float %18310 0
      %15491 = OpFSub %float %14404 %10824
      %10869 = OpCompositeExtract %float %18310 1
      %20240 = OpFSub %float %10869 %10926
      %20339 = OpCompositeConstruct %v2float %15491 %20240
       %8396 = OpFAdd %float %14404 %10824
      %24389 = OpFAdd %float %10869 %10926
       %7501 = OpCompositeConstruct %v2float %8396 %24389
      %18249 = OpFMul %float %float_n2 %17353
      %19368 = OpFAdd %float %18249 %float_3
      %20699 = OpLoad %510 %xe_fxaa_source
       %7603 = OpImageSampleExplicitLod %v4float %20699 %20339 Lod %float_0
      %18553 = OpCompositeExtract %float %7603 3
      %19810 = OpFMul %float %17353 %17353
      %22664 = OpLoad %510 %xe_fxaa_source
      %11494 = OpImageSampleExplicitLod %v4float %22664 %7501 Lod %float_0
      %14550 = OpCompositeExtract %float %11494 3
      %20362 = OpLogicalNot %bool %17727
      %24202 = OpSelect %float %20362 %13777 %10697
      %22551 = OpFMul %float %6817 %float_0_25
      %11306 = OpFMul %float %24202 %float_0_5
      %18652 = OpFSub %float %23382 %11306
      %18462 = OpFMul %float %19368 %19810
      %20782 = OpFOrdLessThan %bool %18652 %float_0
       %8292 = OpFSub %float %18553 %11306
      %20116 = OpFSub %float %14550 %11306
       %7894 = OpExtInst %float %1 FAbs %8292
      %14707 = OpFOrdGreaterThanEqual %bool %7894 %22551
      %12853 = OpExtInst %float %1 FAbs %20116
       %7500 = OpFOrdGreaterThanEqual %bool %12853 %22551
       %9520 = OpLogicalNot %bool %14707
               OpSelectionMerge %21913 None
               OpBranchConditional %9520 %20766 %21913
      %20766 = OpLabel
      %21429 = OpFMul %float %10824 %float_1_5
       %9060 = OpFSub %float %15491 %21429
       %7721 = OpCompositeInsert %v2float %9060 %20339 0
               OpBranch %21913
      %21913 = OpLabel
      %10928 = OpPhi %v2float %20339 %18756 %7721 %20766
               OpSelectionMerge %20409 None
               OpBranchConditional %9520 %19817 %20409
      %19817 = OpLabel
      %11939 = OpFMul %float %10926 %float_1_5
      %13026 = OpCompositeExtract %float %10928 1
      %23787 = OpFSub %float %13026 %11939
      %23596 = OpCompositeInsert %v2float %23787 %10928 1
               OpBranch %20409
      %20409 = OpLabel
      %22995 = OpPhi %v2float %10928 %21913 %23596 %19817
      %16953 = OpLogicalNot %bool %7500
      %15637 = OpLogicalOr %bool %9520 %16953
               OpSelectionMerge %21914 None
               OpBranchConditional %16953 %20729 %21914
      %20729 = OpLabel
      %21776 = OpFMul %float %10824 %float_1_5
       %6376 = OpFAdd %float %8396 %21776
      %14677 = OpCompositeInsert %v2float %6376 %7501 0
               OpBranch %21914
      %21914 = OpLabel
      %10929 = OpPhi %v2float %7501 %20409 %14677 %20729
               OpSelectionMerge %21915 None
               OpBranchConditional %16953 %19818 %21915
      %19818 = OpLabel
      %11902 = OpFMul %float %10926 %float_1_5
      %13373 = OpCompositeExtract %float %10929 1
      %21103 = OpFAdd %float %13373 %11902
      %24095 = OpCompositeInsert %v2float %21103 %10929 1
               OpBranch %21915
      %21915 = OpLabel
      %10930 = OpPhi %v2float %10929 %21914 %24095 %19818
               OpSelectionMerge %21265 None
               OpBranchConditional %15637 %22376 %21265
      %22376 = OpLabel
               OpSelectionMerge %17876 None
               OpBranchConditional %9520 %13334 %17876
      %13334 = OpLabel
       %8473 = OpLoad %510 %xe_fxaa_source
      %25068 = OpImageSampleExplicitLod %v4float %8473 %22995 Lod %float_0
      %21109 = OpCompositeExtract %float %25068 3
               OpBranch %17876
      %17876 = OpLabel
      %10931 = OpPhi %float %8292 %22376 %21109 %13334
               OpSelectionMerge %17877 None
               OpBranchConditional %16953 %13335 %17877
      %13335 = OpLabel
       %8474 = OpLoad %510 %xe_fxaa_source
      %25069 = OpImageSampleExplicitLod %v4float %8474 %10930 Lod %float_0
      %21110 = OpCompositeExtract %float %25069 3
               OpBranch %17877
      %17877 = OpLabel
      %10932 = OpPhi %float %20116 %17876 %21110 %13335
               OpSelectionMerge %6844 None
               OpBranchConditional %9520 %23354 %6844
      %23354 = OpLabel
      %20555 = OpFSub %float %10931 %11306
               OpBranch %6844
       %6844 = OpLabel
      %10933 = OpPhi %float %10931 %17877 %20555 %23354
               OpSelectionMerge %21454 None
               OpBranchConditional %16953 %23355 %21454
      %23355 = OpLabel
      %20556 = OpFSub %float %10932 %11306
               OpBranch %21454
      %21454 = OpLabel
      %18283 = OpPhi %float %10932 %6844 %20556 %23355
       %9827 = OpExtInst %float %1 FAbs %10933
      %10093 = OpFOrdGreaterThanEqual %bool %9827 %22551
      %12854 = OpExtInst %float %1 FAbs %18283
       %7502 = OpFOrdGreaterThanEqual %bool %12854 %22551
       %9521 = OpLogicalNot %bool %10093
               OpSelectionMerge %21916 None
               OpBranchConditional %9521 %19819 %21916
      %19819 = OpLabel
      %11940 = OpFMul %float %10824 %float_2
      %13027 = OpCompositeExtract %float %22995 0
      %23788 = OpFSub %float %13027 %11940
      %23597 = OpCompositeInsert %v2float %23788 %22995 0
               OpBranch %21916
      %21916 = OpLabel
      %10934 = OpPhi %v2float %22995 %21454 %23597 %19819
               OpSelectionMerge %20410 None
               OpBranchConditional %9521 %19820 %20410
      %19820 = OpLabel
      %11941 = OpFMul %float %10926 %float_2
      %13028 = OpCompositeExtract %float %10934 1
      %23789 = OpFSub %float %13028 %11941
      %23598 = OpCompositeInsert %v2float %23789 %10934 1
               OpBranch %20410
      %20410 = OpLabel
      %22996 = OpPhi %v2float %10934 %21916 %23598 %19820
      %16954 = OpLogicalNot %bool %7502
      %15638 = OpLogicalOr %bool %9521 %16954
               OpSelectionMerge %21918 None
               OpBranchConditional %16954 %19821 %21918
      %19821 = OpLabel
      %11903 = OpFMul %float %10824 %float_2
      %13374 = OpCompositeExtract %float %10930 0
      %21104 = OpFAdd %float %13374 %11903
      %24096 = OpCompositeInsert %v2float %21104 %10930 0
               OpBranch %21918
      %21918 = OpLabel
      %10935 = OpPhi %v2float %10930 %20410 %24096 %19821
               OpSelectionMerge %21919 None
               OpBranchConditional %16954 %19822 %21919
      %19822 = OpLabel
      %11904 = OpFMul %float %10926 %float_2
      %13375 = OpCompositeExtract %float %10935 1
      %21105 = OpFAdd %float %13375 %11904
      %24097 = OpCompositeInsert %v2float %21105 %10935 1
               OpBranch %21919
      %21919 = OpLabel
      %10936 = OpPhi %v2float %10935 %21918 %24097 %19822
               OpSelectionMerge %21264 None
               OpBranchConditional %15638 %22377 %21264
      %22377 = OpLabel
               OpSelectionMerge %17878 None
               OpBranchConditional %9521 %13336 %17878
      %13336 = OpLabel
       %8475 = OpLoad %510 %xe_fxaa_source
      %25070 = OpImageSampleExplicitLod %v4float %8475 %22996 Lod %float_0
      %21111 = OpCompositeExtract %float %25070 3
               OpBranch %17878
      %17878 = OpLabel
      %10937 = OpPhi %float %10933 %22377 %21111 %13336
               OpSelectionMerge %17879 None
               OpBranchConditional %16954 %13337 %17879
      %13337 = OpLabel
       %8476 = OpLoad %510 %xe_fxaa_source
      %25071 = OpImageSampleExplicitLod %v4float %8476 %10936 Lod %float_0
      %21112 = OpCompositeExtract %float %25071 3
               OpBranch %17879
      %17879 = OpLabel
      %10938 = OpPhi %float %18283 %17878 %21112 %13337
               OpSelectionMerge %6845 None
               OpBranchConditional %9521 %23356 %6845
      %23356 = OpLabel
      %20557 = OpFSub %float %10937 %11306
               OpBranch %6845
       %6845 = OpLabel
      %10939 = OpPhi %float %10937 %17879 %20557 %23356
               OpSelectionMerge %21455 None
               OpBranchConditional %16954 %23357 %21455
      %23357 = OpLabel
      %20558 = OpFSub %float %10938 %11306
               OpBranch %21455
      %21455 = OpLabel
      %18284 = OpPhi %float %10938 %6845 %20558 %23357
       %9828 = OpExtInst %float %1 FAbs %10939
      %10094 = OpFOrdGreaterThanEqual %bool %9828 %22551
      %12855 = OpExtInst %float %1 FAbs %18284
       %7503 = OpFOrdGreaterThanEqual %bool %12855 %22551
       %9522 = OpLogicalNot %bool %10094
               OpSelectionMerge %21920 None
               OpBranchConditional %9522 %19823 %21920
      %19823 = OpLabel
      %11942 = OpFMul %float %10824 %float_4
      %13029 = OpCompositeExtract %float %22996 0
      %23790 = OpFSub %float %13029 %11942
      %23599 = OpCompositeInsert %v2float %23790 %22996 0
               OpBranch %21920
      %21920 = OpLabel
      %10940 = OpPhi %v2float %22996 %21455 %23599 %19823
               OpSelectionMerge %20411 None
               OpBranchConditional %9522 %19824 %20411
      %19824 = OpLabel
      %11943 = OpFMul %float %10926 %float_4
      %13030 = OpCompositeExtract %float %10940 1
      %23791 = OpFSub %float %13030 %11943
      %23600 = OpCompositeInsert %v2float %23791 %10940 1
               OpBranch %20411
      %20411 = OpLabel
      %22997 = OpPhi %v2float %10940 %21920 %23600 %19824
      %16955 = OpLogicalNot %bool %7503
      %15639 = OpLogicalOr %bool %9522 %16955
               OpSelectionMerge %21921 None
               OpBranchConditional %16955 %19825 %21921
      %19825 = OpLabel
      %11905 = OpFMul %float %10824 %float_4
      %13376 = OpCompositeExtract %float %10936 0
      %21106 = OpFAdd %float %13376 %11905
      %24098 = OpCompositeInsert %v2float %21106 %10936 0
               OpBranch %21921
      %21921 = OpLabel
      %10941 = OpPhi %v2float %10936 %20411 %24098 %19825
               OpSelectionMerge %21922 None
               OpBranchConditional %16955 %19826 %21922
      %19826 = OpLabel
      %11906 = OpFMul %float %10926 %float_4
      %13377 = OpCompositeExtract %float %10941 1
      %21107 = OpFAdd %float %13377 %11906
      %24099 = OpCompositeInsert %v2float %21107 %10941 1
               OpBranch %21922
      %21922 = OpLabel
      %10942 = OpPhi %v2float %10941 %21921 %24099 %19826
               OpSelectionMerge %21263 None
               OpBranchConditional %15639 %22378 %21263
      %22378 = OpLabel
               OpSelectionMerge %17880 None
               OpBranchConditional %9522 %13338 %17880
      %13338 = OpLabel
       %8477 = OpLoad %510 %xe_fxaa_source
      %25072 = OpImageSampleExplicitLod %v4float %8477 %22997 Lod %float_0
      %21113 = OpCompositeExtract %float %25072 3
               OpBranch %17880
      %17880 = OpLabel
      %10943 = OpPhi %float %10939 %22378 %21113 %13338
               OpSelectionMerge %17881 None
               OpBranchConditional %16955 %13339 %17881
      %13339 = OpLabel
       %8478 = OpLoad %510 %xe_fxaa_source
      %25073 = OpImageSampleExplicitLod %v4float %8478 %10942 Lod %float_0
      %21114 = OpCompositeExtract %float %25073 3
               OpBranch %17881
      %17881 = OpLabel
      %10944 = OpPhi %float %18284 %17880 %21114 %13339
               OpSelectionMerge %6846 None
               OpBranchConditional %9522 %23358 %6846
      %23358 = OpLabel
      %20559 = OpFSub %float %10943 %11306
               OpBranch %6846
       %6846 = OpLabel
      %10945 = OpPhi %float %10943 %17881 %20559 %23358
               OpSelectionMerge %21456 None
               OpBranchConditional %16955 %23359 %21456
      %23359 = OpLabel
      %20560 = OpFSub %float %10944 %11306
               OpBranch %21456
      %21456 = OpLabel
      %18285 = OpPhi %float %10944 %6846 %20560 %23359
       %9829 = OpExtInst %float %1 FAbs %10945
      %10095 = OpFOrdGreaterThanEqual %bool %9829 %22551
      %12856 = OpExtInst %float %1 FAbs %18285
       %7504 = OpFOrdGreaterThanEqual %bool %12856 %22551
       %9523 = OpLogicalNot %bool %10095
               OpSelectionMerge %21923 None
               OpBranchConditional %9523 %19827 %21923
      %19827 = OpLabel
      %11944 = OpFMul %float %10824 %float_12
      %13031 = OpCompositeExtract %float %22997 0
      %23792 = OpFSub %float %13031 %11944
      %23601 = OpCompositeInsert %v2float %23792 %22997 0
               OpBranch %21923
      %21923 = OpLabel
      %10946 = OpPhi %v2float %22997 %21456 %23601 %19827
               OpSelectionMerge %20412 None
               OpBranchConditional %9523 %19828 %20412
      %19828 = OpLabel
      %11945 = OpFMul %float %10926 %float_12
      %13032 = OpCompositeExtract %float %10946 1
      %23793 = OpFSub %float %13032 %11945
      %23602 = OpCompositeInsert %v2float %23793 %10946 1
               OpBranch %20412
      %20412 = OpLabel
      %24534 = OpPhi %v2float %10946 %21923 %23602 %19828
      %22068 = OpLogicalNot %bool %7504
               OpSelectionMerge %21924 None
               OpBranchConditional %22068 %19829 %21924
      %19829 = OpLabel
      %11907 = OpFMul %float %10824 %float_12
      %13379 = OpCompositeExtract %float %10942 0
      %21108 = OpFAdd %float %13379 %11907
      %24100 = OpCompositeInsert %v2float %21108 %10942 0
               OpBranch %21924
      %21924 = OpLabel
      %10947 = OpPhi %v2float %10942 %20412 %24100 %19829
               OpSelectionMerge %21948 None
               OpBranchConditional %22068 %19830 %21948
      %19830 = OpLabel
      %11908 = OpFMul %float %10926 %float_12
      %13380 = OpCompositeExtract %float %10947 1
      %21115 = OpFAdd %float %13380 %11908
      %24101 = OpCompositeInsert %v2float %21115 %10947 1
               OpBranch %21948
      %21948 = OpLabel
      %10540 = OpPhi %v2float %10947 %21924 %24101 %19830
               OpBranch %21263
      %21263 = OpLabel
      %11175 = OpPhi %float %18284 %21922 %18285 %21948
      %14344 = OpPhi %float %10939 %21922 %10945 %21948
      %15229 = OpPhi %v2float %10942 %21922 %10540 %21948
      %14518 = OpPhi %v2float %22997 %21922 %24534 %21948
               OpBranch %21264
      %21264 = OpLabel
      %11176 = OpPhi %float %18283 %21919 %11175 %21263
      %14345 = OpPhi %float %10933 %21919 %14344 %21263
      %15230 = OpPhi %v2float %10936 %21919 %15229 %21263
      %14519 = OpPhi %v2float %22996 %21919 %14518 %21263
               OpBranch %21265
      %21265 = OpLabel
      %11177 = OpPhi %float %20116 %21915 %11176 %21264
      %14346 = OpPhi %float %8292 %21915 %14345 %21264
      %12037 = OpPhi %v2float %10930 %21915 %15230 %21264
      %22288 = OpPhi %v2float %22995 %21915 %14519 %21264
      %15213 = OpCompositeExtract %float %22288 0
      %15492 = OpFSub %float %14661 %15213
      %14042 = OpCompositeExtract %float %12037 0
      %12719 = OpFSub %float %14042 %14661
               OpSelectionMerge %6847 None
               OpBranchConditional %9632 %10691 %6847
      %10691 = OpLabel
      %12005 = OpCompositeExtract %float %22288 1
      %13369 = OpFSub %float %13870 %12005
               OpBranch %6847
       %6847 = OpLabel
      %10948 = OpPhi %float %15492 %21265 %13369 %10691
               OpSelectionMerge %24718 None
               OpBranchConditional %9632 %10692 %24718
      %10692 = OpLabel
      %12006 = OpCompositeExtract %float %12037 1
      %13370 = OpFSub %float %12006 %13870
               OpBranch %24718
      %24718 = OpLabel
      %20208 = OpPhi %float %12719 %6847 %13370 %10692
      %17293 = OpFOrdLessThan %bool %14346 %float_0
      %13178 = OpLogicalNotEqual %bool %17293 %20782
      %22904 = OpFAdd %float %20208 %10948
      %12339 = OpFOrdLessThan %bool %11177 %float_0
      %22504 = OpLogicalNotEqual %bool %12339 %20782
      %22919 = OpFOrdLessThan %bool %10948 %20208
       %8965 = OpExtInst %float %1 FMin %10948 %20208
      %22016 = OpSelect %bool %22919 %13178 %22504
      %23114 = OpFMul %float %18462 %18462
      %24447 = OpFDiv %float %float_n1 %22904
      %20313 = OpFMul %float %8965 %24447
      %21330 = OpFAdd %float %20313 %float_0_5
      %19334 = OpFMul %float %23114 %float_0_75
      %21391 = OpSelect %float %22016 %21330 %float_0
      %15140 = OpExtInst %float %1 FMax %21391 %19334
               OpSelectionMerge %21925 None
               OpBranchConditional %9632 %20730 %21925
      %20730 = OpLabel
      %21777 = OpFMul %float %15140 %17200
       %6377 = OpFAdd %float %14661 %21777
      %14678 = OpCompositeInsert %v2float %6377 %19130 0
               OpBranch %21925
      %21925 = OpLabel
      %10950 = OpPhi %v2float %19130 %24718 %14678 %20730
               OpSelectionMerge %18376 None
               OpBranchConditional %21917 %19831 %18376
      %19831 = OpLabel
      %11909 = OpFMul %float %15140 %17200
      %13381 = OpCompositeExtract %float %10950 1
      %21116 = OpFAdd %float %13381 %11909
      %24102 = OpCompositeInsert %v2float %21116 %10950 1
               OpBranch %18376
      %18376 = OpLabel
      %20953 = OpPhi %v2float %10950 %21925 %24102 %19831
      %14064 = OpLoad %510 %xe_fxaa_source
      %22685 = OpImageSampleExplicitLod %v4float %14064 %20953 Lod %float_0
       %8858 = OpCompositeExtract %float %22685 0
      %22672 = OpCompositeExtract %float %22685 1
      %11025 = OpCompositeExtract %float %22685 2
       %9033 = OpCompositeConstruct %v4float %8858 %22672 %11025 %23382
               OpBranch %16763
      %16763 = OpLabel
      %21637 = OpPhi %v4float %23004 %21993 %9033 %18376
       %7836 = OpLoad %166 %xe_fxaa_dest
      %13802 = OpBitcast %v2int %21717
      %24522 = OpCompositeExtract %float %21637 0
      %13264 = OpCompositeExtract %float %21637 1
       %8175 = OpCompositeExtract %float %21637 2
      %15931 = OpCompositeConstruct %v4float %24522 %13264 %8175 %float_1
               OpImageWrite %7836 %13802 %15931
               OpBranch %21573
      %21573 = OpLabel
               OpReturn
               OpFunctionEnd
#endif

const uint32_t fxaa_cs[] = {
    0x07230203, 0x00010000, 0x0008000B, 0x000061F2, 0x00000000, 0x00020011,
    0x00000001, 0x00020011, 0x00000031, 0x0006000B, 0x00000001, 0x4C534C47,
    0x6474732E, 0x3035342E, 0x00000000, 0x0003000E, 0x00000000, 0x00000001,
    0x0006000F, 0x00000005, 0x0000161F, 0x6E69616D, 0x00000000, 0x00000F48,
    0x00060010, 0x0000161F, 0x00000011, 0x00000010, 0x00000008, 0x00000001,
    0x00030003, 0x00000002, 0x000001CC, 0x000A0004, 0x475F4C47, 0x4C474F4F,
    0x70635F45, 0x74735F70, 0x5F656C79, 0x656E696C, 0x7269645F, 0x69746365,
    0x00006576, 0x00080004, 0x475F4C47, 0x4C474F4F, 0x6E695F45, 0x64756C63,
    0x69645F65, 0x74636572, 0x00657669, 0x00040005, 0x0000161F, 0x6E69616D,
    0x00000000, 0x00080005, 0x00000F48, 0x475F6C67, 0x61626F6C, 0x766E496C,
    0x7461636F, 0x496E6F69, 0x00000044, 0x00060005, 0x00000402, 0x61617846,
    0x736E6F43, 0x746E6174, 0x00000073, 0x00070006, 0x00000402, 0x00000000,
    0x665F6578, 0x5F616178, 0x657A6973, 0x00000000, 0x00080006, 0x00000402,
    0x00000001, 0x665F6578, 0x5F616178, 0x657A6973, 0x766E695F, 0x00000000,
    0x00030005, 0x00001342, 0x00000000, 0x00060005, 0x00000FA8, 0x665F6578,
    0x5F616178, 0x72756F73, 0x00006563, 0x00060005, 0x00001141, 0x665F6578,
    0x5F616178, 0x74736564, 0x00000000, 0x00040047, 0x00000F48, 0x0000000B,
    0x0000001C, 0x00030047, 0x00000402, 0x00000002, 0x00050048, 0x00000402,
    0x00000000, 0x00000023, 0x00000000, 0x00050048, 0x00000402, 0x00000001,
    0x00000023, 0x00000008, 0x00040047, 0x00000FA8, 0x00000021, 0x00000000,
    0x00040047, 0x00000FA8, 0x00000022, 0x00000001, 0x00030047, 0x00001141,
    0x00000019, 0x00040047, 0x00001141, 0x00000021, 0x00000000, 0x00040047,
    0x00001141, 0x00000022, 0x00000000, 0x00040047, 0x00000B0F, 0x0000000B,
    0x00000019, 0x00020013, 0x00000008, 0x00030021, 0x00000502, 0x00000008,
    0x00030016, 0x0000000D, 0x00000020, 0x00040017, 0x0000001D, 0x0000000D,
    0x00000004, 0x00040017, 0x00000013, 0x0000000D, 0x00000002, 0x00090019,
    0x00000096, 0x0000000D, 0x00000001, 0x00000000, 0x00000000, 0x00000000,
    0x00000001, 0x00000000, 0x0003001B, 0x000001FE, 0x00000096, 0x00040020,
    0x0000047B, 0x00000000, 0x000001FE, 0x00040015, 0x0000000B, 0x00000020,
    0x00000000, 0x0004002B, 0x0000000B, 0x00000A0A, 0x00000000, 0x0004002B,
    0x0000000B, 0x00000A0D, 0x00000001, 0x0004002B, 0x0000000D, 0x00000A0C,
    0x00000000, 0x00040015, 0x0000000C, 0x00000020, 0x00000001, 0x0004002B,
    0x0000000C, 0x00000A14, 0x00000003, 0x00040017, 0x00000012, 0x0000000C,
    0x00000002, 0x0004002B, 0x0000000C, 0x00000A08, 0xFFFFFFFF, 0x0005002C,
    0x00000012, 0x00000702, 0x00000A08, 0x00000A08, 0x00020014, 0x00000009,
    0x0004002B, 0x0000000C, 0x00000A0E, 0x00000001, 0x0005002C, 0x00000012,
    0x00000714, 0x00000A0E, 0x00000A08, 0x0005002C, 0x00000012, 0x0000071A,
    0x00000A08, 0x00000A0E, 0x0004002B, 0x0000000D, 0x0000008A, 0x3F800000,
    0x0004002B, 0x0000000D, 0x000002CF, 0xC0000000, 0x0004002B, 0x0000000D,
    0x00000018, 0x40000000, 0x0004002B, 0x0000000D, 0x0000022D, 0x3DAAAAAB,
    0x0004002B, 0x0000000D, 0x000000FC, 0x3F000000, 0x0004002B, 0x0000000D,
    0x00000BA2, 0x40400000, 0x0004002B, 0x0000000D, 0x00000B69, 0x40800000,
    0x0004002B, 0x0000000D, 0x00000051, 0x3FC00000, 0x0004002B, 0x0000000D,
    0x00000ABE, 0x41400000, 0x00040017, 0x00000011, 0x0000000B, 0x00000002,
    0x00040017, 0x00000014, 0x0000000B, 0x00000003, 0x00040020, 0x00000291,
    0x00000001, 0x00000014, 0x0004003B, 0x00000291, 0x00000F48, 0x00000001,
    0x0004001E, 0x00000402, 0x00000011, 0x00000013, 0x00040020, 0x0000067F,
    0x00000009, 0x00000402, 0x0004003B, 0x0000067F, 0x00001342, 0x00000009,
    0x0004002B, 0x0000000C, 0x00000A0B, 0x00000000, 0x00040020, 0x0000028E,
    0x00000009, 0x00000011, 0x00040017, 0x0000000F, 0x00000009, 0x00000002,
    0x00040020, 0x00000290, 0x00000009, 0x00000013, 0x0004003B, 0x0000047B,
    0x00000FA8, 0x00000000, 0x0004002B, 0x0000000D, 0x000000C3, 0x3F400000,
    0x0004002B, 0x0000000D, 0x000006B6, 0x3E29FBE7, 0x0004002B, 0x0000000D,
    0x0000088E, 0x3DAA9931, 0x00090019, 0x000000A6, 0x0000000D, 0x00000001,
    0x00000000, 0x00000000, 0x00000000, 0x00000002, 0x0000000B, 0x00040020,
    0x00000323, 0x00000000, 0x000000A6, 0x0004003B, 0x00000323, 0x00001141,
    0x00000000, 0x0004002B, 0x0000000B, 0x00000A3A, 0x00000010, 0x0004002B,
    0x0000000B, 0x00000A22, 0x00000008, 0x0006002C, 0x00000014, 0x00000B0F,
    0x00000A3A, 0x00000A22, 0x00000A0D, 0x0005002C, 0x00000013, 0x0000061E,
    0x000000FC, 0x000000FC, 0x0004002B, 0x0000000D, 0x0000016E, 0x3E800000,
    0x0004002B, 0x0000000D, 0x00000341, 0xBF800000, 0x00030001, 0x0000001D,
    0x00000002, 0x00050036, 0x00000008, 0x0000161F, 0x00000000, 0x00000502,
    0x000200F8, 0x00003B06, 0x000300F7, 0x00005445, 0x00000000, 0x000300FB,
    0x00000A0A, 0x00003272, 0x000200F8, 0x00003272, 0x0004003D, 0x00000014,
    0x000035C1, 0x00000F48, 0x0007004F, 0x00000011, 0x000054D5, 0x000035C1,
    0x000035C1, 0x00000000, 0x00000001, 0x00050041, 0x0000028E, 0x00001E50,
    0x00001342, 0x00000A0B, 0x0004003D, 0x00000011, 0x00003442, 0x00001E50,
    0x000500AE, 0x0000000F, 0x00005B8D, 0x000054D5, 0x00003442, 0x0004009A,
    0x00000009, 0x00005A24, 0x00005B8D, 0x000300F7, 0x00002566, 0x00000000,
    0x000400FA, 0x00005A24, 0x000055E8, 0x00002566, 0x000200F8, 0x000055E8,
    0x000200F9, 0x00005445, 0x000200F8, 0x00002566, 0x00040070, 0x00000013,
    0x000045A7, 0x000054D5, 0x00050081, 0x00000013, 0x00005E7B, 0x000045A7,
    0x0000061E, 0x00050041, 0x00000290, 0x00002E57, 0x00001342, 0x00000A0E,
    0x0004003D, 0x00000013, 0x00005D0D, 0x00002E57, 0x00050085, 0x00000013,
    0x00004ABA, 0x00005E7B, 0x00005D0D, 0x000300F7, 0x0000417B, 0x00000000,
    0x000300FB, 0x00000A0A, 0x00002514, 0x000200F8, 0x00002514, 0x00050051,
    0x0000000D, 0x00003945, 0x00004ABA, 0x00000000, 0x00050051, 0x0000000D,
    0x0000362E, 0x00004ABA, 0x00000001, 0x0004003D, 0x000001FE, 0x000043D1,
    0x00000FA8, 0x00070058, 0x0000001D, 0x000059DC, 0x000043D1, 0x00004ABA,
    0x00000002, 0x00000A0C, 0x0004003D, 0x000001FE, 0x00004317, 0x00000FA8,
    0x00060060, 0x0000001D, 0x000042C2, 0x00004317, 0x00004ABA, 0x00000A14,
    0x0004003D, 0x000001FE, 0x00002392, 0x00000FA8, 0x00080060, 0x0000001D,
    0x0000341C, 0x00002392, 0x00004ABA, 0x00000A14, 0x00000008, 0x00000702,
    0x00050051, 0x0000000D, 0x00002DE8, 0x000042C2, 0x00000000, 0x00050051,
    0x0000000D, 0x00005B56, 0x000059DC, 0x00000003, 0x0007000C, 0x0000000D,
    0x00002AD6, 0x00000001, 0x00000028, 0x00002DE8, 0x00005B56, 0x0007000C,
    0x0000000D, 0x0000459E, 0x00000001, 0x00000025, 0x00002DE8, 0x00005B56,
    0x00050051, 0x0000000D, 0x00001A93, 0x000042C2, 0x00000002, 0x0007000C,
    0x0000000D, 0x00005162, 0x00000001, 0x00000028, 0x00001A93, 0x00002AD6,
    0x0007000C, 0x0000000D, 0x00004A95, 0x00000001, 0x00000025, 0x00001A93,
    0x0000459E, 0x00050051, 0x0000000D, 0x00002C1D, 0x0000341C, 0x00000002,
    0x00050051, 0x0000000D, 0x00005F3D, 0x0000341C, 0x00000000, 0x0007000C,
    0x0000000D, 0x000025DF, 0x00000001, 0x00000028, 0x00002C1D, 0x00005F3D,
    0x0007000C, 0x0000000D, 0x00003414, 0x00000001, 0x00000025, 0x00002C1D,
    0x00005F3D, 0x0007000C, 0x0000000D, 0x000041DA, 0x00000001, 0x00000028,
    0x000025DF, 0x00005162, 0x0007000C, 0x0000000D, 0x000051B3, 0x00000001,
    0x00000025, 0x00003414, 0x00004A95, 0x00050085, 0x0000000D, 0x0000224F,
    0x000041DA, 0x000006B6, 0x00050083, 0x0000000D, 0x00002A0A, 0x000041DA,
    0x000051B3, 0x0007000C, 0x0000000D, 0x00004379, 0x00000001, 0x00000028,
    0x0000088E, 0x0000224F, 0x000500B8, 0x00000009, 0x00004EA1, 0x00002A0A,
    0x00004379, 0x000300F7, 0x000044D2, 0x00000000, 0x000400FA, 0x00004EA1,
    0x000055E9, 0x000044D2, 0x000200F8, 0x000055E9, 0x000200F9, 0x0000417B,
    0x000200F8, 0x000044D2, 0x0004003D, 0x000001FE, 0x00002A8F, 0x00000FA8,
    0x00080058, 0x0000001D, 0x000053F8, 0x00002A8F, 0x00004ABA, 0x0000000A,
    0x00000A0C, 0x00000714, 0x00050051, 0x0000000D, 0x000030A3, 0x000053F8,
    0x00000003, 0x0004003D, 0x000001FE, 0x00004EBE, 0x00000FA8, 0x00080058,
    0x0000001D, 0x00004ECB, 0x00004EBE, 0x00004ABA, 0x0000000A, 0x00000A0C,
    0x0000071A, 0x00050051, 0x0000000D, 0x00005039, 0x00004ECB, 0x00000003,
    0x00050081, 0x0000000D, 0x00005883, 0x00002C1D, 0x00002DE8, 0x00050081,
    0x0000000D, 0x000048E4, 0x00005F3D, 0x00001A93, 0x00050088, 0x0000000D,
    0x00001BFD, 0x0000008A, 0x00002A0A, 0x00050081, 0x0000000D, 0x00002AC5,
    0x00005883, 0x000048E4, 0x00050085, 0x0000000D, 0x00003AA6, 0x000002CF,
    0x00005B56, 0x00050081, 0x0000000D, 0x0000460D, 0x00003AA6, 0x00005883,
    0x00050081, 0x0000000D, 0x0000300C, 0x00003AA6, 0x000048E4, 0x00050051,
    0x0000000D, 0x000046EF, 0x000042C2, 0x00000001, 0x00050081, 0x0000000D,
    0x00003D99, 0x000030A3, 0x000046EF, 0x00050051, 0x0000000D, 0x000031B0,
    0x0000341C, 0x00000003, 0x00050081, 0x0000000D, 0x00001A75, 0x000031B0,
    0x000030A3, 0x00050085, 0x0000000D, 0x00001866, 0x000002CF, 0x00001A93,
    0x00050081, 0x0000000D, 0x000046E9, 0x00001866, 0x00003D99, 0x00050085,
    0x0000000D, 0x00002496, 0x000002CF, 0x00002C1D, 0x00050081, 0x0000000D,
    0x0000499D, 0x00002496, 0x00001A75, 0x00050081, 0x0000000D, 0x00005170,
    0x000031B0, 0x00005039, 0x00050081, 0x0000000D, 0x0000387B, 0x00005039,
    0x000046EF, 0x0006000C, 0x0000000D, 0x00005A39, 0x00000001, 0x00000004,
    0x0000460D, 0x00050085, 0x0000000D, 0x000052F0, 0x00005A39, 0x00000018,
    0x0006000C, 0x0000000D, 0x00005AB9, 0x00000001, 0x00000004, 0x000046E9,
    0x00050081, 0x0000000D, 0x00004ADC, 0x000052F0, 0x00005AB9, 0x0006000C,
    0x0000000D, 0x00002AA3, 0x00000001, 0x00000004, 0x0000300C, 0x00050085,
    0x0000000D, 0x000052F1, 0x00002AA3, 0x00000018, 0x0006000C, 0x0000000D,
    0x00001931, 0x00000001, 0x00000004, 0x0000499D, 0x00050081, 0x0000000D,
    0x00002BE8, 0x000052F1, 0x00001931, 0x00050085, 0x0000000D, 0x00004C6A,
    0x000002CF, 0x00005F3D, 0x00050081, 0x0000000D, 0x000046EA, 0x00004C6A,
    0x00005170, 0x00050085, 0x0000000D, 0x00001BEB, 0x000002CF, 0x00002DE8,
    0x00050081, 0x0000000D, 0x00001B12, 0x00001BEB, 0x0000387B, 0x0006000C,
    0x0000000D, 0x00001F08, 0x00000001, 0x00000004, 0x000046EA, 0x00050081,
    0x0000000D, 0x00003DF6, 0x00001F08, 0x00004ADC, 0x0006000C, 0x0000000D,
    0x00003602, 0x00000001, 0x00000004, 0x00001B12, 0x00050081, 0x0000000D,
    0x00001E72, 0x00003602, 0x00002BE8, 0x00050081, 0x0000000D, 0x00005C1B,
    0x00005170, 0x00003D99, 0x00050051, 0x0000000D, 0x00002192, 0x00005D0D,
    0x00000000, 0x000500BE, 0x00000009, 0x0000559D, 0x00003DF6, 0x00001E72,
    0x00050085, 0x0000000D, 0x00003D86, 0x00002AC5, 0x00000018, 0x00050081,
    0x0000000D, 0x0000543E, 0x00003D86, 0x00005C1B, 0x000400A8, 0x00000009,
    0x000025A0, 0x0000559D, 0x000300F7, 0x00005596, 0x00000000, 0x000400FA,
    0x000025A0, 0x000031D8, 0x00005596, 0x000200F8, 0x000031D8, 0x00060052,
    0x0000001D, 0x00005411, 0x00005F3D, 0x00000002, 0x00000002, 0x000200F9,
    0x00005596, 0x000200F8, 0x00005596, 0x000700F5, 0x0000001D, 0x00002AAC,
    0x0000341C, 0x000044D2, 0x00005411, 0x000031D8, 0x000300F7, 0x00005597,
    0x00000000, 0x000400FA, 0x000025A0, 0x000031D9, 0x00005597, 0x000200F8,
    0x000031D9, 0x00060052, 0x0000001D, 0x00005412, 0x00001A93, 0x00000002,
    0x00000000, 0x000200F9, 0x00005597, 0x000200F8, 0x00005597, 0x000700F5,
    0x0000001D, 0x00002AAD, 0x000042C2, 0x00005596, 0x00005412, 0x000031D9,
    0x000300F7, 0x00003D5D, 0x00000000, 0x000400FA, 0x0000559D, 0x00003285,
    0x00003D5D, 0x000200F8, 0x00003285, 0x00050051, 0x0000000D, 0x00002993,
    0x00005D0D, 0x00000001, 0x000200F9, 0x00003D5D, 0x000200F8, 0x00003D5D,
    0x000700F5, 0x0000000D, 0x00002462, 0x00002192, 0x00005597, 0x00002993,
    0x00003285, 0x00050085, 0x0000000D, 0x00005040, 0x0000543E, 0x0000022D,
    0x00050083, 0x0000000D, 0x00001E78, 0x00005040, 0x00005B56, 0x00050051,
    0x0000000D, 0x00003704, 0x00002AAC, 0x00000002, 0x00050083, 0x0000000D,
    0x00004E89, 0x00003704, 0x00005B56, 0x00050051, 0x0000000D, 0x00002E18,
    0x00002AAD, 0x00000000, 0x00050083, 0x0000000D, 0x00002DF3, 0x00002E18,
    0x00005B56, 0x00050081, 0x0000000D, 0x000029C9, 0x00003704, 0x00005B56,
    0x00050081, 0x0000000D, 0x000035D1, 0x00002E18, 0x00005B56, 0x0006000C,
    0x0000000D, 0x00003B94, 0x00000001, 0x00000004, 0x00004E89, 0x0006000C,
    0x0000000D, 0x000052D8, 0x00000001, 0x00000004, 0x00002DF3, 0x000500BE,
    0x00000009, 0x0000453F, 0x00003B94, 0x000052D8, 0x0007000C, 0x0000000D,
    0x00001AA1, 0x00000001, 0x00000028, 0x00003B94, 0x000052D8, 0x000300F7,
    0x0000606D, 0x00000000, 0x000400FA, 0x0000453F, 0x00005DEE, 0x0000606D,
    0x000200F8, 0x00005DEE, 0x0004007F, 0x0000000D, 0x00003B51, 0x00002462,
    0x000200F9, 0x0000606D, 0x000200F8, 0x0000606D, 0x000700F5, 0x0000000D,
    0x00004330, 0x00002462, 0x00003D5D, 0x00003B51, 0x00005DEE, 0x0006000C,
    0x0000000D, 0x00004D0F, 0x00000001, 0x00000004, 0x00001E78, 0x00050085,
    0x0000000D, 0x0000296E, 0x00004D0F, 0x00001BFD, 0x0008000C, 0x0000000D,
    0x000043C9, 0x00000001, 0x0000002B, 0x0000296E, 0x00000A0C, 0x0000008A,
    0x000600A9, 0x0000000D, 0x00002A48, 0x000025A0, 0x00000A0C, 0x00002192,
    0x000300F7, 0x000045D3, 0x00000000, 0x000400FA, 0x0000559D, 0x000055EA,
    0x00004341, 0x000200F8, 0x000055EA, 0x000200F9, 0x000045D3, 0x000200F8,
    0x00004341, 0x00050051, 0x0000000D, 0x00003309, 0x00005D0D, 0x00000001,
    0x000200F9, 0x000045D3, 0x000200F8, 0x000045D3, 0x000700F5, 0x0000000D,
    0x00002AAE, 0x00000A0C, 0x000055EA, 0x00003309, 0x00004341, 0x000300F7,
    0x00005598, 0x00000000, 0x000400FA, 0x000025A0, 0x000050F8, 0x00005598,
    0x000200F8, 0x000050F8, 0x00050085, 0x0000000D, 0x0000550F, 0x00004330,
    0x000000FC, 0x00050081, 0x0000000D, 0x000018E7, 0x00003945, 0x0000550F,
    0x00060052, 0x00000013, 0x00003954, 0x000018E7, 0x00004ABA, 0x00000000,
    0x000200F9, 0x00005598, 0x000200F8, 0x00005598, 0x000700F5, 0x00000013,
    0x00002AAF, 0x00004ABA, 0x000045D3, 0x00003954, 0x000050F8, 0x000300F7,
    0x00004944, 0x00000000, 0x000400FA, 0x0000559D, 0x00004D68, 0x00004944,
    0x000200F8, 0x00004D68, 0x00050085, 0x0000000D, 0x00002E7D, 0x00004330,
    0x000000FC, 0x00050051, 0x0000000D, 0x0000343C, 0x00002AAF, 0x00000001,
    0x00050081, 0x0000000D, 0x0000526E, 0x0000343C, 0x00002E7D, 0x00060052,
    0x00000013, 0x00005E1E, 0x0000526E, 0x00002AAF, 0x00000001, 0x000200F9,
    0x00004944, 0x000200F8, 0x00004944, 0x000700F5, 0x00000013, 0x00004786,
    0x00002AAF, 0x00005598, 0x00005E1E, 0x00004D68, 0x00050051, 0x0000000D,
    0x00003844, 0x00004786, 0x00000000, 0x00050083, 0x0000000D, 0x00003C83,
    0x00003844, 0x00002A48, 0x00050051, 0x0000000D, 0x00002A75, 0x00004786,
    0x00000001, 0x00050083, 0x0000000D, 0x00004F10, 0x00002A75, 0x00002AAE,
    0x00050050, 0x00000013, 0x00004F73, 0x00003C83, 0x00004F10, 0x00050081,
    0x0000000D, 0x000020CC, 0x00003844, 0x00002A48, 0x00050081, 0x0000000D,
    0x00005F45, 0x00002A75, 0x00002AAE, 0x00050050, 0x00000013, 0x00001D4D,
    0x000020CC, 0x00005F45, 0x00050085, 0x0000000D, 0x00004749, 0x000002CF,
    0x000043C9, 0x00050081, 0x0000000D, 0x00004BA8, 0x00004749, 0x00000BA2,
    0x0004003D, 0x000001FE, 0x000050DB, 0x00000FA8, 0x00070058, 0x0000001D,
    0x00001DB3, 0x000050DB, 0x00004F73, 0x00000002, 0x00000A0C, 0x00050051,
    0x0000000D, 0x00004879, 0x00001DB3, 0x00000003, 0x00050085, 0x0000000D,
    0x00004D62, 0x000043C9, 0x000043C9, 0x0004003D, 0x000001FE, 0x00005888,
    0x00000FA8, 0x00070058, 0x0000001D, 0x00002CE6, 0x00005888, 0x00001D4D,
    0x00000002, 0x00000A0C, 0x00050051, 0x0000000D, 0x000038D6, 0x00002CE6,
    0x00000003, 0x000400A8, 0x00000009, 0x00004F8A, 0x0000453F, 0x000600A9,
    0x0000000D, 0x00005E8A, 0x00004F8A, 0x000035D1, 0x000029C9, 0x00050085,
    0x0000000D, 0x00005817, 0x00001AA1, 0x0000016E, 0x00050085, 0x0000000D,
    0x00002C2A, 0x00005E8A, 0x000000FC, 0x00050083, 0x0000000D, 0x000048DC,
    0x00005B56, 0x00002C2A, 0x00050085, 0x0000000D, 0x0000481E, 0x00004BA8,
    0x00004D62, 0x000500B8, 0x00000009, 0x0000512E, 0x000048DC, 0x00000A0C,
    0x00050083, 0x0000000D, 0x00002064, 0x00004879, 0x00002C2A, 0x00050083,
    0x0000000D, 0x00004E94, 0x000038D6, 0x00002C2A, 0x0006000C, 0x0000000D,
    0x00001ED6, 0x00000001, 0x00000004, 0x00002064, 0x000500BE, 0x00000009,
    0x00003973, 0x00001ED6, 0x00005817, 0x0006000C, 0x0000000D, 0x00003235,
    0x00000001, 0x00000004, 0x00004E94, 0x000500BE, 0x00000009, 0x00001D4C,
    0x00003235, 0x00005817, 0x000400A8, 0x00000009, 0x00002530, 0x00003973,
    0x000300F7, 0x00005599, 0x00000000, 0x000400FA, 0x00002530, 0x0000511E,
    0x00005599, 0x000200F8, 0x0000511E, 0x00050085, 0x0000000D, 0x000053B5,
    0x00002A48, 0x00000051, 0x00050083, 0x0000000D, 0x00002364, 0x00003C83,
    0x000053B5, 0x00060052, 0x00000013, 0x00001E29, 0x00002364, 0x00004F73,
    0x00000000, 0x000200F9, 0x00005599, 0x000200F8, 0x00005599, 0x000700F5,
    0x00000013, 0x00002AB0, 0x00004F73, 0x00004944, 0x00001E29, 0x0000511E,
    0x000300F7, 0x00004FB9, 0x00000000, 0x000400FA, 0x00002530, 0x00004D69,
    0x00004FB9, 0x000200F8, 0x00004D69, 0x00050085, 0x0000000D, 0x00002EA3,
    0x00002AAE, 0x00000051, 0x00050051, 0x0000000D, 0x000032E2, 0x00002AB0,
    0x00000001, 0x00050083, 0x0000000D, 0x00005CEB, 0x000032E2, 0x00002EA3,
    0x00060052, 0x00000013, 0x00005C2C, 0x00005CEB, 0x00002AB0, 0x00000001,
    0x000200F9, 0x00004FB9, 0x000200F8, 0x00004FB9, 0x000700F5, 0x00000013,
    0x000059D3, 0x00002AB0, 0x00005599, 0x00005C2C, 0x00004D69, 0x000400A8,
    0x00000009, 0x00004239, 0x00001D4C, 0x000500A6, 0x00000009, 0x00003D15,
    0x00002530, 0x00004239, 0x000300F7, 0x0000559A, 0x00000000, 0x000400FA,
    0x00004239, 0x000050F9, 0x0000559A, 0x000200F8, 0x000050F9, 0x00050085,
    0x0000000D, 0x00005510, 0x00002A48, 0x00000051, 0x00050081, 0x0000000D,
    0x000018E8, 0x000020CC, 0x00005510, 0x00060052, 0x00000013, 0x00003955,
    0x000018E8, 0x00001D4D, 0x00000000, 0x000200F9, 0x0000559A, 0x000200F8,
    0x0000559A, 0x000700F5, 0x00000013, 0x00002AB1, 0x00001D4D, 0x00004FB9,
    0x00003955, 0x000050F9, 0x000300F7, 0x0000559B, 0x00000000, 0x000400FA,
    0x00004239, 0x00004D6A, 0x0000559B, 0x000200F8, 0x00004D6A, 0x00050085,
    0x0000000D, 0x00002E7E, 0x00002AAE, 0x00000051, 0x00050051, 0x0000000D,
    0x0000343D, 0x00002AB1, 0x00000001, 0x00050081, 0x0000000D, 0x0000526F,
    0x0000343D, 0x00002E7E, 0x00060052, 0x00000013, 0x00005E1F, 0x0000526F,
    0x00002AB1, 0x00000001, 0x000200F9, 0x0000559B, 0x000200F8, 0x0000559B,
    0x000700F5, 0x00000013, 0x00002AB2, 0x00002AB1, 0x0000559A, 0x00005E1F,
    0x00004D6A, 0x000300F7, 0x00005311, 0x00000000, 0x000400FA, 0x00003D15,
    0x00005768, 0x00005311, 0x000200F8, 0x00005768, 0x000300F7, 0x000045D4,
    0x00000000, 0x000400FA, 0x00002530, 0x00003416, 0x000045D4, 0x000200F8,
    0x00003416, 0x0004003D, 0x000001FE, 0x00002119, 0x00000FA8, 0x00070058,
    0x0000001D, 0x000061EC, 0x00002119, 0x000059D3, 0x00000002, 0x00000A0C,
    0x00050051, 0x0000000D, 0x00005275, 0x000061EC, 0x00000003, 0x000200F9,
    0x000045D4, 0x000200F8, 0x000045D4, 0x000700F5, 0x0000000D, 0x00002AB3,
    0x00002064, 0x00005768, 0x00005275, 0x00003416, 0x000300F7, 0x000045D5,
    0x00000000, 0x000400FA, 0x00004239, 0x00003417, 0x000045D5, 0x000200F8,
    0x00003417, 0x0004003D, 0x000001FE, 0x0000211A, 0x00000FA8, 0x00070058,
    0x0000001D, 0x000061ED, 0x0000211A, 0x00002AB2, 0x00000002, 0x00000A0C,
    0x00050051, 0x0000000D, 0x00005276, 0x000061ED, 0x00000003, 0x000200F9,
    0x000045D5, 0x000200F8, 0x000045D5, 0x000700F5, 0x0000000D, 0x00002AB4,
    0x00004E94, 0x000045D4, 0x00005276, 0x00003417, 0x000300F7, 0x00001ABC,
    0x00000000, 0x000400FA, 0x00002530, 0x00005B3A, 0x00001ABC, 0x000200F8,
    0x00005B3A, 0x00050083, 0x0000000D, 0x0000504B, 0x00002AB3, 0x00002C2A,
    0x000200F9, 0x00001ABC, 0x000200F8, 0x00001ABC, 0x000700F5, 0x0000000D,
    0x00002AB5, 0x00002AB3, 0x000045D5, 0x0000504B, 0x00005B3A, 0x000300F7,
    0x000053CE, 0x00000000, 0x000400FA, 0x00004239, 0x00005B3B, 0x000053CE,
    0x000200F8, 0x00005B3B, 0x00050083, 0x0000000D, 0x0000504C, 0x00002AB4,
    0x00002C2A, 0x000200F9, 0x000053CE, 0x000200F8, 0x000053CE, 0x000700F5,
    0x0000000D, 0x0000476B, 0x00002AB4, 0x00001ABC, 0x0000504C, 0x00005B3B,
    0x0006000C, 0x0000000D, 0x00002663, 0x00000001, 0x00000004, 0x00002AB5,
    0x000500BE, 0x00000009, 0x0000276D, 0x00002663, 0x00005817, 0x0006000C,
    0x0000000D, 0x00003236, 0x00000001, 0x00000004, 0x0000476B, 0x000500BE,
    0x00000009, 0x00001D4E, 0x00003236, 0x00005817, 0x000400A8, 0x00000009,
    0x00002531, 0x0000276D, 0x000300F7, 0x0000559C, 0x00000000, 0x000400FA,
    0x00002531, 0x00004D6B, 0x0000559C, 0x000200F8, 0x00004D6B, 0x00050085,
    0x0000000D, 0x00002EA4, 0x00002A48, 0x00000018, 0x00050051, 0x0000000D,
    0x000032E3, 0x000059D3, 0x00000000, 0x00050083, 0x0000000D, 0x00005CEC,
    0x000032E3, 0x00002EA4, 0x00060052, 0x00000013, 0x00005C2D, 0x00005CEC,
    0x000059D3, 0x00000000, 0x000200F9, 0x0000559C, 0x000200F8, 0x0000559C,
    0x000700F5, 0x00000013, 0x00002AB6, 0x000059D3, 0x000053CE, 0x00005C2D,
    0x00004D6B, 0x000300F7, 0x00004FBA, 0x00000000, 0x000400FA, 0x00002531,
    0x00004D6C, 0x00004FBA, 0x000200F8, 0x00004D6C, 0x00050085, 0x0000000D,
    0x00002EA5, 0x00002AAE, 0x00000018, 0x00050051, 0x0000000D, 0x000032E4,
    0x00002AB6, 0x00000001, 0x00050083, 0x0000000D, 0x00005CED, 0x000032E4,
    0x00002EA5, 0x00060052, 0x00000013, 0x00005C2E, 0x00005CED, 0x00002AB6,
    0x00000001, 0x000200F9, 0x00004FBA, 0x000200F8, 0x00004FBA, 0x000700F5,
    0x00000013, 0x000059D4, 0x00002AB6, 0x0000559C, 0x00005C2E, 0x00004D6C,
    0x000400A8, 0x00000009, 0x0000423A, 0x00001D4E, 0x000500A6, 0x00000009,
    0x00003D16, 0x00002531, 0x0000423A, 0x000300F7, 0x0000559E, 0x00000000,
    0x000400FA, 0x0000423A, 0x00004D6D, 0x0000559E, 0x000200F8, 0x00004D6D,
    0x00050085, 0x0000000D, 0x00002E7F, 0x00002A48, 0x00000018, 0x00050051,
    0x0000000D, 0x0000343E, 0x00002AB2, 0x00000000, 0x00050081, 0x0000000D,
    0x00005270, 0x0000343E, 0x00002E7F, 0x00060052, 0x00000013, 0x00005E20,
    0x00005270, 0x00002AB2, 0x00000000, 0x000200F9, 0x0000559E, 0x000200F8,
    0x0000559E, 0x000700F5, 0x00000013, 0x00002AB7, 0x00002AB2, 0x00004FBA,
    0x00005E20, 0x00004D6D, 0x000300F7, 0x0000559F, 0x00000000, 0x000400FA,
    0x0000423A, 0x00004D6E, 0x0000559F, 0x000200F8, 0x00004D6E, 0x00050085,
    0x0000000D, 0x00002E80, 0x00002AAE, 0x00000018, 0x00050051, 0x0000000D,
    0x0000343F, 0x00002AB7, 0x00000001, 0x00050081, 0x0000000D, 0x00005271,
    0x0000343F, 0x00002E80, 0x00060052, 0x00000013, 0x00005E21, 0x00005271,
    0x00002AB7, 0x00000001, 0x000200F9, 0x0000559F, 0x000200F8, 0x0000559F,
    0x000700F5, 0x00000013, 0x00002AB8, 0x00002AB7, 0x0000559E, 0x00005E21,
    0x00004D6E, 0x000300F7, 0x00005310, 0x00000000, 0x000400FA, 0x00003D16,
    0x00005769, 0x00005310, 0x000200F8, 0x00005769, 0x000300F7, 0x000045D6,
    0x00000000, 0x000400FA, 0x00002531, 0x00003418, 0x000045D6, 0x000200F8,
    0x00003418, 0x0004003D, 0x000001FE, 0x0000211B, 0x00000FA8, 0x00070058,
    0x0000001D, 0x000061EE, 0x0000211B, 0x000059D4, 0x00000002, 0x00000A0C,
    0x00050051, 0x0000000D, 0x00005277, 0x000061EE, 0x00000003, 0x000200F9,
    0x000045D6, 0x000200F8, 0x000045D6, 0x000700F5, 0x0000000D, 0x00002AB9,
    0x00002AB5, 0x00005769, 0x00005277, 0x00003418, 0x000300F7, 0x000045D7,
    0x00000000, 0x000400FA, 0x0000423A, 0x00003419, 0x000045D7, 0x000200F8,
    0x00003419, 0x0004003D, 0x000001FE, 0x0000211C, 0x00000FA8, 0x00070058,
    0x0000001D, 0x000061EF, 0x0000211C, 0x00002AB8, 0x00000002, 0x00000A0C,
    0x00050051, 0x0000000D, 0x00005278, 0x000061EF, 0x00000003, 0x000200F9,
    0x000045D7, 0x000200F8, 0x000045D7, 0x000700F5, 0x0000000D, 0x00002ABA,
    0x0000476B, 0x000045D6, 0x00005278, 0x00003419, 0x000300F7, 0x00001ABD,
    0x00000000, 0x000400FA, 0x00002531, 0x00005B3C, 0x00001ABD, 0x000200F8,
    0x00005B3C, 0x00050083, 0x0000000D, 0x0000504D, 0x00002AB9, 0x00002C2A,
    0x000200F9, 0x00001ABD, 0x000200F8, 0x00001ABD, 0x000700F5, 0x0000000D,
    0x00002ABB, 0x00002AB9, 0x000045D7, 0x0000504D, 0x00005B3C, 0x000300F7,
    0x000053CF, 0x00000000, 0x000400FA, 0x0000423A, 0x00005B3D, 0x000053CF,
    0x000200F8, 0x00005B3D, 0x00050083, 0x0000000D, 0x0000504E, 0x00002ABA,
    0x00002C2A, 0x000200F9, 0x000053CF, 0x000200F8, 0x000053CF, 0x000700F5,
    0x0000000D, 0x0000476C, 0x00002ABA, 0x00001ABD, 0x0000504E, 0x00005B3D,
    0x0006000C, 0x0000000D, 0x00002664, 0x00000001, 0x00000004, 0x00002ABB,
    0x000500BE, 0x00000009, 0x0000276E, 0x00002664, 0x00005817, 0x0006000C,
    0x0000000D, 0x00003237, 0x00000001, 0x00000004, 0x0000476C, 0x000500BE,
    0x00000009, 0x00001D4F, 0x00003237, 0x00005817, 0x000400A8, 0x00000009,
    0x00002532, 0x0000276E, 0x000300F7, 0x000055A0, 0x00000000, 0x000400FA,
    0x00002532, 0x00004D6F, 0x000055A0, 0x000200F8, 0x00004D6F, 0x00050085,
    0x0000000D, 0x00002EA6, 0x00002A48, 0x00000B69, 0x00050051, 0x0000000D,
    0x000032E5, 0x000059D4, 0x00000000, 0x00050083, 0x0000000D, 0x00005CEE,
    0x000032E5, 0x00002EA6, 0x00060052, 0x00000013, 0x00005C2F, 0x00005CEE,
    0x000059D4, 0x00000000, 0x000200F9, 0x000055A0, 0x000200F8, 0x000055A0,
    0x000700F5, 0x00000013, 0x00002ABC, 0x000059D4, 0x000053CF, 0x00005C2F,
    0x00004D6F, 0x000300F7, 0x00004FBB, 0x00000000, 0x000400FA, 0x00002532,
    0x00004D70, 0x00004FBB, 0x000200F8, 0x00004D70, 0x00050085, 0x0000000D,
    0x00002EA7, 0x00002AAE, 0x00000B69, 0x00050051, 0x0000000D, 0x000032E6,
    0x00002ABC, 0x00000001, 0x00050083, 0x0000000D, 0x00005CEF, 0x000032E6,
    0x00002EA7, 0x00060052, 0x00000013, 0x00005C30, 0x00005CEF, 0x00002ABC,
    0x00000001, 0x000200F9, 0x00004FBB, 0x000200F8, 0x00004FBB, 0x000700F5,
    0x00000013, 0x000059D5, 0x00002ABC, 0x000055A0, 0x00005C30, 0x00004D70,
    0x000400A8, 0x00000009, 0x0000423B, 0x00001D4F, 0x000500A6, 0x00000009,
    0x00003D17, 0x00002532, 0x0000423B, 0x000300F7, 0x000055A1, 0x00000000,
    0x000400FA, 0x0000423B, 0x00004D71, 0x000055A1, 0x000200F8, 0x00004D71,
    0x00050085, 0x0000000D, 0x00002E81, 0x00002A48, 0x00000B69, 0x00050051,
    0x0000000D, 0x00003440, 0x00002AB8, 0x00000000, 0x00050081, 0x0000000D,
    0x00005272, 0x00003440, 0x00002E81, 0x00060052, 0x00000013, 0x00005E22,
    0x00005272, 0x00002AB8, 0x00000000, 0x000200F9, 0x000055A1, 0x000200F8,
    0x000055A1, 0x000700F5, 0x00000013, 0x00002ABD, 0x00002AB8, 0x00004FBB,
    0x00005E22, 0x00004D71, 0x000300F7, 0x000055A2, 0x00000000, 0x000400FA,
    0x0000423B, 0x00004D72, 0x000055A2, 0x000200F8, 0x00004D72, 0x00050085,
    0x0000000D, 0x00002E82, 0x00002AAE, 0x00000B69, 0x00050051, 0x0000000D,
    0x00003441, 0x00002ABD, 0x00000001, 0x00050081, 0x0000000D, 0x00005273,
    0x00003441, 0x00002E82, 0x00060052, 0x00000013, 0x00005E23, 0x00005273,
    0x00002ABD, 0x00000001, 0x000200F9, 0x000055A2, 0x000200F8, 0x000055A2,
    0x000700F5, 0x00000013, 0x00002ABE, 0x00002ABD, 0x000055A1, 0x00005E23,
    0x00004D72, 0x000300F7, 0x0000530F, 0x00000000, 0x000400FA, 0x00003D17,
    0x0000576A, 0x0000530F, 0x000200F8, 0x0000576A, 0x000300F7, 0x000045D8,
    0x00000000, 0x000400FA, 0x00002532, 0x0000341A, 0x000045D8, 0x000200F8,
    0x0000341A, 0x0004003D, 0x000001FE, 0x0000211D, 0x00000FA8, 0x00070058,
    0x0000001D, 0x000061F0, 0x0000211D, 0x000059D5, 0x00000002, 0x00000A0C,
    0x00050051, 0x0000000D, 0x00005279, 0x000061F0, 0x00000003, 0x000200F9,
    0x000045D8, 0x000200F8, 0x000045D8, 0x000700F5, 0x0000000D, 0x00002ABF,
    0x00002ABB, 0x0000576A, 0x00005279, 0x0000341A, 0x000300F7, 0x000045D9,
    0x00000000, 0x000400FA, 0x0000423B, 0x0000341B, 0x000045D9, 0x000200F8,
    0x0000341B, 0x0004003D, 0x000001FE, 0x0000211E, 0x00000FA8, 0x00070058,
    0x0000001D, 0x000061F1, 0x0000211E, 0x00002ABE, 0x00000002, 0x00000A0C,
    0x00050051, 0x0000000D, 0x0000527A, 0x000061F1, 0x00000003, 0x000200F9,
    0x000045D9, 0x000200F8, 0x000045D9, 0x000700F5, 0x0000000D, 0x00002AC0,
    0x0000476C, 0x000045D8, 0x0000527A, 0x0000341B, 0x000300F7, 0x00001ABE,
    0x00000000, 0x000400FA, 0x00002532, 0x00005B3E, 0x00001ABE, 0x000200F8,
    0x00005B3E, 0x00050083, 0x0000000D, 0x0000504F, 0x00002ABF, 0x00002C2A,
    0x000200F9, 0x00001ABE, 0x000200F8, 0x00001ABE, 0x000700F5, 0x0000000D,
    0x00002AC1, 0x00002ABF, 0x000045D9, 0x0000504F, 0x00005B3E, 0x000300F7,
    0x000053D0, 0x00000000, 0x000400FA, 0x0000423B, 0x00005B3F, 0x000053D0,
    0x000200F8, 0x00005B3F, 0x00050083, 0x0000000D, 0x00005050, 0x00002AC0,
    0x00002C2A, 0x000200F9, 0x000053D0, 0x000200F8, 0x000053D0, 0x000700F5,
    0x0000000D, 0x0000476D, 0x00002AC0, 0x00001ABE, 0x00005050, 0x00005B3F,
    0x0006000C, 0x0000000D, 0x00002665, 0x00000001, 0x00000004, 0x00002AC1,
    0x000500BE, 0x00000009, 0x0000276F, 0x00002665, 0x00005817, 0x0006000C,
    0x0000000D, 0x00003238, 0x00000001, 0x00000004, 0x0000476D, 0x000500BE,
    0x00000009, 0x00001D50, 0x00003238, 0x00005817, 0x000400A8, 0x00000009,
    0x00002533, 0x0000276F, 0x000300F7, 0x000055A3, 0x00000000, 0x000400FA,
    0x00002533, 0x00004D73, 0x000055A3, 0x000200F8, 0x00004D73, 0x00050085,
    0x0000000D, 0x00002EA8, 0x00002A48, 0x00000ABE, 0x00050051, 0x0000000D,
    0x000032E7, 0x000059D5, 0x00000000, 0x00050083, 0x0000000D, 0x00005CF0,
    0x000032E7, 0x00002EA8, 0x00060052, 0x00000013, 0x00005C31, 0x00005CF0,
    0x000059D5, 0x00000000, 0x000200F9, 0x000055A3, 0x000200F8, 0x000055A3,
    0x000700F5, 0x00000013, 0x00002AC2, 0x000059D5, 0x000053D0, 0x00005C31,
    0x00004D73, 0x000300F7, 0x00004FBC, 0x00000000, 0x000400FA, 0x00002533,
    0x00004D74, 0x00004FBC, 0x000200F8, 0x00004D74, 0x00050085, 0x0000000D,
    0x00002EA9, 0x00002AAE, 0x00000ABE, 0x00050051, 0x0000000D, 0x000032E8,
    0x00002AC2, 0x00000001, 0x00050083, 0x0000000D, 0x00005CF1, 0x000032E8,
    0x00002EA9, 0x00060052, 0x00000013, 0x00005C32, 0x00005CF1, 0x00002AC2,
    0x00000001, 0x000200F9, 0x00004FBC, 0x000200F8, 0x00004FBC, 0x000700F5,
    0x00000013, 0x00005FD6, 0x00002AC2, 0x000055A3, 0x00005C32, 0x00004D74,
    0x000400A8, 0x00000009, 0x00005634, 0x00001D50, 0x000300F7, 0x000055A4,
    0x00000000, 0x000400FA, 0x00005634, 0x00004D75, 0x000055A4, 0x000200F8,
    0x00004D75, 0x00050085, 0x0000000D, 0x00002E83, 0x00002A48, 0x00000ABE,
    0x00050051, 0x0000000D, 0x00003443, 0x00002ABE, 0x00000000, 0x00050081,
    0x0000000D, 0x00005274, 0x00003443, 0x00002E83, 0x00060052, 0x00000013,
    0x00005E24, 0x00005274, 0x00002ABE, 0x00000000, 0x000200F9, 0x000055A4,
    0x000200F8, 0x000055A4, 0x000700F5, 0x00000013, 0x00002AC3, 0x00002ABE,
    0x00004FBC, 0x00005E24, 0x00004D75, 0x000300F7, 0x000055BC, 0x00000000,
    0x000400FA, 0x00005634, 0x00004D76, 0x000055BC, 0x000200F8, 0x00004D76,
    0x00050085, 0x0000000D, 0x00002E84, 0x00002AAE, 0x00000ABE, 0x00050051,
    0x0000000D, 0x00003444, 0x00002AC3, 0x00000001, 0x00050081, 0x0000000D,
    0x0000527B, 0x00003444, 0x00002E84, 0x00060052, 0x00000013, 0x00005E25,
    0x0000527B, 0x00002AC3, 0x00000001, 0x000200F9, 0x000055BC, 0x000200F8,
    0x000055BC, 0x000700F5, 0x00000013, 0x0000292C, 0x00002AC3, 0x000055A4,
    0x00005E25, 0x00004D76, 0x000200F9, 0x0000530F, 0x000200F8, 0x0000530F,
    0x000700F5, 0x0000000D, 0x00002BA7, 0x0000476C, 0x000055A2, 0x0000476D,
    0x000055BC, 0x000700F5, 0x0000000D, 0x00003808, 0x00002ABB, 0x000055A2,
    0x00002AC1, 0x000055BC, 0x000700F5, 0x00000013, 0x00003B7D, 0x00002ABE,
    0x000055A2, 0x0000292C, 0x000055BC, 0x000700F5, 0x00000013, 0x000038B6,
    0x000059D5, 0x000055A2, 0x00005FD6, 0x000055BC, 0x000200F9, 0x00005310,
    0x000200F8, 0x00005310, 0x000700F5, 0x0000000D, 0x00002BA8, 0x0000476B,
    0x0000559F, 0x00002BA7, 0x0000530F, 0x000700F5, 0x0000000D, 0x00003809,
    0x00002AB5, 0x0000559F, 0x00003808, 0x0000530F, 0x000700F5, 0x00000013,
    0x00003B7E, 0x00002AB8, 0x0000559F, 0x00003B7D, 0x0000530F, 0x000700F5,
    0x00000013, 0x000038B7, 0x000059D4, 0x0000559F, 0x000038B6, 0x0000530F,
    0x000200F9, 0x00005311, 0x000200F8, 0x00005311, 0x000700F5, 0x0000000D,
    0x00002BA9, 0x00004E94, 0x0000559B, 0x00002BA8, 0x00005310, 0x000700F5,
    0x0000000D, 0x0000380A, 0x00002064, 0x0000559B, 0x00003809, 0x00005310,
    0x000700F5, 0x00000013, 0x00002F05, 0x00002AB2, 0x0000559B, 0x00003B7E,
    0x00005310, 0x000700F5, 0x00000013, 0x00005710, 0x000059D3, 0x0000559B,
    0x000038B7, 0x00005310, 0x00050051, 0x0000000D, 0x00003B6D, 0x00005710,
    0x00000000, 0x00050083, 0x0000000D, 0x00003C84, 0x00003945, 0x00003B6D,
    0x00050051, 0x0000000D, 0x000036DA, 0x00002F05, 0x00000000, 0x00050083,
    0x0000000D, 0x000031AF, 0x000036DA, 0x00003945, 0x000300F7, 0x00001ABF,
    0x00000000, 0x000400FA, 0x000025A0, 0x000029C3, 0x00001ABF, 0x000200F8,
    0x000029C3, 0x00050051, 0x0000000D, 0x00002EE5, 0x00005710, 0x00000001,
    0x00050083, 0x0000000D, 0x00003439, 0x0000362E, 0x00002EE5, 0x000200F9,
    0x00001ABF, 0x000200F8, 0x00001ABF, 0x000700F5, 0x0000000D, 0x00002AC4,
    0x00003C84, 0x00005311, 0x00003439, 0x000029C3, 0x000300F7, 0x0000608E,
    0x00000000, 0x000400FA, 0x000025A0, 0x000029C4, 0x0000608E, 0x000200F8,
    0x000029C4, 0x00050051, 0x0000000D, 0x00002EE6, 0x00002F05, 0x00000001,
    0x00050083, 0x0000000D, 0x0000343A, 0x00002EE6, 0x0000362E, 0x000200F9,
    0x0000608E, 0x000200F8, 0x0000608E, 0x000700F5, 0x0000000D, 0x00004EF0,
    0x000031AF, 0x00001ABF, 0x0000343A, 0x000029C4, 0x000500B8, 0x00000009,
    0x0000438D, 0x0000380A, 0x00000A0C, 0x000500A5, 0x00000009, 0x0000337A,
    0x0000438D, 0x0000512E, 0x00050081, 0x0000000D, 0x00005978, 0x00004EF0,
    0x00002AC4, 0x000500B8, 0x00000009, 0x00003033, 0x00002BA9, 0x00000A0C,
    0x000500A5, 0x00000009, 0x000057E8, 0x00003033, 0x0000512E, 0x000500B8,
    0x00000009, 0x00005987, 0x00002AC4, 0x00004EF0, 0x0007000C, 0x0000000D,
    0x00002305, 0x00000001, 0x00000025, 0x00002AC4, 0x00004EF0, 0x000600A9,
    0x00000009, 0x00005600, 0x00005987, 0x0000337A, 0x000057E8, 0x00050085,
    0x0000000D, 0x00005A4A, 0x0000481E, 0x0000481E, 0x00050088, 0x0000000D,
    0x00005F7F, 0x00000341, 0x00005978, 0x00050085, 0x0000000D, 0x00004F59,
    0x00002305, 0x00005F7F, 0x00050081, 0x0000000D, 0x00005352, 0x00004F59,
    0x000000FC, 0x00050085, 0x0000000D, 0x00004B86, 0x00005A4A, 0x000000C3,
    0x000600A9, 0x0000000D, 0x0000538F, 0x00005600, 0x00005352, 0x00000A0C,
    0x0007000C, 0x0000000D, 0x00003B24, 0x00000001, 0x00000028, 0x0000538F,
    0x00004B86, 0x000300F7, 0x000055A5, 0x00000000, 0x000400FA, 0x000025A0,
    0x000050FA, 0x000055A5, 0x000200F8, 0x000050FA, 0x00050085, 0x0000000D,
    0x00005511, 0x00003B24, 0x00004330, 0x00050081, 0x0000000D, 0x000018E9,
    0x00003945, 0x00005511, 0x00060052, 0x00000013, 0x00003956, 0x000018E9,
    0x00004ABA, 0x00000000, 0x000200F9, 0x000055A5, 0x000200F8, 0x000055A5,
    0x000700F5, 0x00000013, 0x00002AC6, 0x00004ABA, 0x0000608E, 0x00003956,
    0x000050FA, 0x000300F7, 0x000047C8, 0x00000000, 0x000400FA, 0x0000559D,
    0x00004D77, 0x000047C8, 0x000200F8, 0x00004D77, 0x00050085, 0x0000000D,
    0x00002E85, 0x00003B24, 0x00004330, 0x00050051, 0x0000000D, 0x00003445,
    0x00002AC6, 0x00000001, 0x00050081, 0x0000000D, 0x0000527C, 0x00003445,
    0x00002E85, 0x00060052, 0x00000013, 0x00005E26, 0x0000527C, 0x00002AC6,
    0x00000001, 0x000200F9, 0x000047C8, 0x000200F8, 0x000047C8, 0x000700F5,
    0x00000013, 0x000051D9, 0x00002AC6, 0x000055A5, 0x00005E26, 0x00004D77,
    0x0004003D, 0x000001FE, 0x000036F0, 0x00000FA8, 0x00070058, 0x0000001D,
    0x0000589D, 0x000036F0, 0x000051D9, 0x00000002, 0x00000A0C, 0x00050051,
    0x0000000D, 0x0000229A, 0x0000589D, 0x00000000, 0x00050051, 0x0000000D,
    0x00005890, 0x0000589D, 0x00000001, 0x00050051, 0x0000000D, 0x00002B11,
    0x0000589D, 0x00000002, 0x00070050, 0x0000001D, 0x00002349, 0x0000229A,
    0x00005890, 0x00002B11, 0x00005B56, 0x000200F9, 0x0000417B, 0x000200F8,
    0x0000417B, 0x000700F5, 0x0000001D, 0x00005485, 0x000059DC, 0x000055E9,
    0x00002349, 0x000047C8, 0x0004003D, 0x000000A6, 0x00001E9C, 0x00001141,
    0x0004007C, 0x00000012, 0x000035EA, 0x000054D5, 0x00050051, 0x0000000D,
    0x00005FCA, 0x00005485, 0x00000000, 0x00050051, 0x0000000D, 0x000033D0,
    0x00005485, 0x00000001, 0x00050051, 0x0000000D, 0x00001FEF, 0x00005485,
    0x00000002, 0x00070050, 0x0000001D, 0x00003E3B, 0x00005FCA, 0x000033D0,
    0x00001FEF, 0x0000008A, 0x00040063, 0x00001E9C, 0x000035EA, 0x00003E3B,
    0x000200F9, 0x00005445, 0x000200F8, 0x00005445, 0x000100FD, 0x00010038,
};
