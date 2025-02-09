// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT License.

#include "precomp.h"

namespace Dml
{

class DmlOperatorPadding : public DmlOperator, public PaddingHelper
{
public:
    DmlOperatorPadding(const MLOperatorKernelCreationContext& kernelInfo, uint32_t opsetVersion)
    :   DmlOperator(kernelInfo),
        PaddingHelper(kernelInfo, kernelInfo.GetTensorShapeDescription(), opsetVersion)
    {
        const uint32_t inputCount = kernelInfo.GetInputCount();
        ML_CHECK_VALID_ARGUMENT((opsetVersion >= 2 && opsetVersion < 11 && inputCount == 1)
                             || (opsetVersion >= 11 && inputCount >= 2 && inputCount <= 4));
        ML_CHECK_VALID_ARGUMENT(kernelInfo.GetOutputCount() == 1);

        std::vector<std::optional<uint32_t>> kernelInputIndices = { 0 }; // Only bind GPU to first 'data' tensor.
        DmlOperator::Initialize(kernelInfo, kernelInputIndices);

        assert(m_inputTensorDescs[0].GetDimensionCount() >= gsl::narrow_cast<uint32_t>(m_startPadding.size()));
        assert(m_inputTensorDescs[0].GetDimensionCount() >= gsl::narrow_cast<uint32_t>(m_endPadding.size()));

        // Pad the parameters to respect DML's requirements
        m_startPadding.insert(
            m_startPadding.begin(),
            m_inputTensorDescs[0].GetDimensionCount() - gsl::narrow_cast<uint32_t>(m_startPadding.size()),
            0);

        m_endPadding.insert(
            m_endPadding.begin(),
            m_inputTensorDescs[0].GetDimensionCount() - gsl::narrow_cast<uint32_t>(m_endPadding.size()),
            0);

        // Convert padding mode.
        DML_PADDING_MODE mode = DML_PADDING_MODE_CONSTANT;
        std::string modeString = kernelInfo.GetOptionalAttribute<std::string>(AttrName::Mode, AttrValue::Reflect);

        if (modeString == AttrValue::Constant)
        {
            mode = DML_PADDING_MODE_CONSTANT;
        }
        else if (modeString == AttrValue::Edge)
        {
            mode = DML_PADDING_MODE_EDGE;
        }
        else if (modeString == AttrValue::Reflect)
        {
            mode = DML_PADDING_MODE_REFLECTION;
        }
#if DML_TARGET_VERSION >= 0x6300
        else if (modeString == AttrValue::Wrap)
        {
            mode = DML_PADDING_MODE_WRAP;
        }
#endif
        else
        {
            ML_INVALID_ARGUMENT("Unknown Pad mode attribute.");
        }

        std::vector<DML_TENSOR_DESC> inputDescs = GetDmlInputDescs();
        std::vector<DML_TENSOR_DESC> outputDescs = GetDmlOutputDescs();

        DML_PADDING1_OPERATOR_DESC paddingDesc = {};
        paddingDesc.InputTensor = inputDescs.data();
        paddingDesc.OutputTensor = outputDescs.data();
        paddingDesc.PaddingMode = mode;
        paddingDesc.DimensionCount = gsl::narrow_cast<uint32_t>(m_startPadding.size());
        paddingDesc.StartPadding = m_startPadding.data();
        paddingDesc.EndPadding = m_endPadding.data();
        // PaddingValueDataType will always be equal to inputDataTensorDataType
        // Assigning paddingValueDataType to inputDataTensorDataType because this field
        // has to be assigned even if program does not go through below conditional
        // logic for some corner test case (like opsetVersion >= 11, but no validInput at index 2)
        // Same applies to paddingValue.
        paddingDesc.PaddingValueDataType = this->m_inputTensorDescs[0].GetDmlDataType();
        CastToClampedScalarUnion<float>(paddingDesc.PaddingValueDataType, 0.0f, /*out*/&paddingDesc.PaddingValue);

        // Read the constant value which can come from an attribute or tensor.
        if (opsetVersion >= 11)
        {
            if (kernelInfo.IsInputValid(2))
            {
                MLOperatorTensor constantPaddingValueTensor = kernelInfo.GetConstantInputTensor(2);
                ReadScalarTensorData(constantPaddingValueTensor, /*out*/ &paddingDesc.PaddingValue.Bytes, sizeof(paddingDesc.PaddingValue.Bytes));
            }
        }
        else
        {
            auto value = kernelInfo.GetOptionalAttribute<float>(AttrName::Value, 0.0f);
            CastToClampedScalarUnion<float>(paddingDesc.PaddingValueDataType, value, /*out*/&paddingDesc.PaddingValue);
        }

        DML_OPERATOR_DESC opDesc = { DML_OPERATOR_PADDING1, &paddingDesc };

        SetDmlOperatorDesc(opDesc, kernelInfo);
    }
};

void CALLBACK QueryPad(IMLOperatorSupportQueryContextPrivate* context, /*out*/ bool* isSupported)
{
    // DML_PADDING1_OPERATOR_DESC doesn't support negative padding counts i.e. StartPadding and EndPadding
    // can't contain negative elements.
    // For opset < 11,
    //      if attribute 'pads' contains negative element, fall back to CPU
    // opset >= 11
    //      DML EP continues to produce wrong result. [TODO: After DML1.9 release, introduce new API for pad to
    //      handle negative values for StartPadding and EndPadding]
    *isSupported = true;

    MLOperatorAttributes attributes(context);

    std::vector<int32_t> padding = attributes.GetOptionalAttributeVectorInt32(AttrName::Pads);
    *isSupported = std::none_of(padding.begin(), padding.end(), [](int32_t padCount) {return padCount < 0; });
}

DML_OP_DEFINE_CREATION_FUNCTION(Pad7, VersionedKernel<DmlOperatorPadding, 7>);
DML_OP_DEFINE_CREATION_FUNCTION(Pad11, VersionedKernel<DmlOperatorPadding, 11>);
DML_OP_DEFINE_CREATION_FUNCTION(Pad13, VersionedKernel<DmlOperatorPadding, 13>);
DML_OP_DEFINE_CREATION_FUNCTION(Pad18, VersionedKernel<DmlOperatorPadding, 18>);
DML_OP_DEFINE_CREATION_FUNCTION(Pad19, VersionedKernel<DmlOperatorPadding, 19>);

} // namespace Dml
