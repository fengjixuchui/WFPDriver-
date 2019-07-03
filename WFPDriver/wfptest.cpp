#ifdef __cplusplus
extern "C" {
#endif

#include "wfptest.h"
// #define INITGUID
#include <guiddef.h>


#define LOG_STRING "WFPDriver: "
#define WFP_DEVICE_NAME L"\\Device\\WFPDriver_Device"
#define WFP_SYM_LINK_NAME L"\\DosDevices\\WFPDriver_Device_SYM"

#define MyKdPrint(_x_)\
	KdPrint((LOG_STRING));\
	KdPrint(_x_)

PDEVICE_OBJECT g_DeviceObject = NULL;
HANDLE g_hEngine = NULL;
UINT32 g_RegisterCalloutId = 0;
UINT32 g_AddCalloutId = 0;
UINT64 g_FilterId = 0;



VOID DriverUnload(PDRIVER_OBJECT DriverObject)
{
	MyKdPrint(("����ж��\n"));
	UNICODE_STRING SymbolicName;
	RtlInitUnicodeString(&SymbolicName, WFP_SYM_LINK_NAME);
	IoDeleteSymbolicLink(&SymbolicName);
	MyKdPrint(("ɾ����������\n"));

	UnInitWFP();
	MyKdPrint(("�ر�WFP���\n"));

	if(g_DeviceObject)
	{
		IoDeleteDevice(g_DeviceObject);
		MyKdPrint(("ɾ���豸\n"));
	}
}


NTSTATUS DriverEntry(PDRIVER_OBJECT DriverObject, PUNICODE_STRING RegistryPath)
{
	UNREFERENCED_PARAMETER(RegistryPath);

	NTSTATUS status = STATUS_SUCCESS;

	MyKdPrint(("Enter DriverEntry\n"));

	DriverObject->DriverUnload = DriverUnload;

	//��ʼ����WFP��ʹ�õ��豸����

	UNICODE_STRING DeviceName;
	UNICODE_STRING SymbolicName;
	RtlInitUnicodeString(&DeviceName, WFP_DEVICE_NAME);
	RtlInitUnicodeString(&SymbolicName, WFP_SYM_LINK_NAME);

	status = IoCreateDevice(DriverObject,
		0,
		&DeviceName,
		FILE_DEVICE_UNKNOWN,
		0,
		FALSE,
		&g_DeviceObject);

	if(!NT_SUCCESS(status))
	{
		return status;
	}

	MyKdPrint(("�豸���󴴽��ɹ�\n"));

	IoCreateSymbolicLink(&SymbolicName, &DeviceName);

	status = InitWFP();

	return status;
}

/*
 * �����ʼ��WFP
 * ��Filter����
 * ע������Callout
 * ����Ӳ�
 * ��ӹ�����
 */
NTSTATUS InitWFP()
{
	NTSTATUS status = STATUS_UNSUCCESSFUL;

	do
	{
		g_hEngine = OpenFilterEngine();
		if(g_hEngine == NULL)
		{
			break;
		}
		status = FwpmTransactionBegin0(g_hEngine, 0);
		if (!NT_SUCCESS(status))
		{
			break;
		}
		if(!NT_SUCCESS(WFPRegisterCallouts(g_DeviceObject)))
		{
			break;
		}
		if(!NT_SUCCESS(WFPAddCallouts()))
		{
			break;
		}
		if(!NT_SUCCESS(WFPAddSubLayers()))
		{
			break;
		}
		if(!NT_SUCCESS(WFPAddFilter()))
		{
			break;
		}
		status = FwpmTransactionCommit0(g_hEngine);
		if (!NT_SUCCESS(status))
		{
			break;
		}
		status = STATUS_SUCCESS;
	}
	while (false);

	return status;
}
NTSTATUS WFPRegisterCallouts(PDEVICE_OBJECT DeviceObject)
{
	FWPS_CALLOUT1 callout = { 0 };
	NTSTATUS status = STATUS_UNSUCCESSFUL;
	UINT32 calloutId;

	callout.calloutKey = WFP_SAMPLE_ESTABLISHED_CALLOUT_V4_GUID;
	callout.flags = 0;
	callout.classifyFn = (FWPS_CALLOUT_CLASSIFY_FN1)WFPClassifyFn1;
	callout.notifyFn = (FWPS_CALLOUT_NOTIFY_FN1)WFPNotifyFn1;
	callout.flowDeleteFn = WFPFlowDeleteFn;

	status = FwpsCalloutRegister1(g_DeviceObject, &callout, &calloutId);

	g_RegisterCalloutId = calloutId;

	return status;
}

NTSTATUS WFPAddCallouts()
{
	NTSTATUS status = STATUS_UNSUCCESSFUL;
	UINT32 calloutId = 0;

	FWPM_CALLOUT callout = { 0 };
	// ���callout֮ǰһ��Ҫȷ��Filter�����Ƿ��
	if(g_hEngine == NULL)
	{
		return status;
	}

	callout.displayData.name = (wchar_t*)L"WFPCalloutName";
	callout.displayData.description = (wchar_t*)L"WFPCalloutDesc";
	// �������ղ�ע���callout
	callout.calloutKey = WFP_SAMPLE_ESTABLISHED_CALLOUT_V4_GUID;
	//��Ҫ�����calloutӦ�õ��ĸ�Filter Layer��
	// ����ο� https://docs.microsoft.com/zh-cn/windows/desktop/FWP/management-filtering-layer-identifiers-
	callout.applicableLayer = FWPM_LAYER_OUTBOUND_TRANSPORT_V4;

	status = FwpmCalloutAdd0(g_hEngine, &callout, NULL, &calloutId);

	g_AddCalloutId = calloutId;

	return status;
}

HANDLE OpenFilterEngine()
{
	FWPM_SESSION0 session = { 0 };
	session.flags = FWPM_SESSION_FLAG_DYNAMIC;

	HANDLE hEngine = NULL;
	FwpmEngineOpen0(NULL,
		RPC_C_AUTHN_WINNT,
		NULL,
		&session,
		&hEngine);
	return hEngine;
}

NTSTATUS WFPAddSubLayers()
{
	NTSTATUS status = STATUS_UNSUCCESSFUL;

	FWPM_SUBLAYER0 subLayer = { 0 };
	subLayer.flags = 0;
	subLayer.displayData.description = (wchar_t*)L"WFPSubLayerDesc";
	subLayer.displayData.name = (wchar_t*)L"WFPSubLayerName";
	subLayer.subLayerKey = WFP_SAMPLE_SUBLAYER_GUID;
	subLayer.weight = 0;

	if(g_hEngine)
	{
		status = FwpmSubLayerAdd0(g_hEngine, &subLayer, NULL);
	}

	return status;
}

NTSTATUS WFPAddFilter()
{
	NTSTATUS status = STATUS_UNSUCCESSFUL;

	FWPM_FILTER0 filter = { 0 };
	FWPM_FILTER_CONDITION0 filterCondition[1] = { 0 };
	UINT64 filterId;
	FWP_V4_ADDR_AND_MASK AddrAndMask = { 0 };

	if(g_hEngine == NULL)
	{
		return status;
	}

	// ��������
	filterCondition[0].fieldKey = FWPM_CONDITION_IP_REMOTE_ADDRESS;
	filterCondition[0].matchType = FWP_MATCH_NOT_EQUAL;
	filterCondition[0].conditionValue.type = FWP_V4_ADDR_MASK;
	filterCondition[0].conditionValue.v4AddrMask = &AddrAndMask;

	filter.displayData.name = (wchar_t*)L"WPFFilterName";
	filter.displayData.description = (wchar_t*)L"WPFFilterDesc";
	filter.flags = 0;
	// �����ֲ�
	// filter.layerKey = FWPM_LAYER_ALE_FLOW_ESTABLISHED_V4;
	filter.layerKey = FWPM_LAYER_OUTBOUND_TRANSPORT_V4;
	// �����Ӳ�
	filter.subLayerKey = WFP_SAMPLE_SUBLAYER_GUID;
	filter.weight.type = FWP_EMPTY;
	filter.numFilterConditions = 1;
	filter.filterCondition = filterCondition;
	filter.action.type = FWP_ACTION_CALLOUT_TERMINATING;
	// ����callout
	filter.action.calloutKey = WFP_SAMPLE_ESTABLISHED_CALLOUT_V4_GUID;


	status = FwpmFilterAdd(g_hEngine, &filter, NULL, &filterId);

	g_FilterId = filterId;

	if(NT_SUCCESS(status))
	{
		return status;
	}


	return status;
}


VOID UnInitWFP()
{
	CloseFilterEngine();
	WFPRemoveFilter();
	WFPRemoveSubLayers();
	WFPRemoveCallouts();
	WFPUnRegisterCallouts();
}

VOID CloseFilterEngine()
{
	if(g_hEngine)
	{
		FwpmEngineClose0(g_hEngine);
	}
	g_hEngine = NULL;
}

VOID WFPUnRegisterCallouts()
{
	if(g_hEngine)
	{
		FwpsCalloutUnregisterById0(g_RegisterCalloutId);
	}
}

VOID WFPRemoveCallouts()
{
	if(g_hEngine)
	{
		FwpmCalloutDeleteById0(g_hEngine, g_AddCalloutId);
	}
}

VOID WFPRemoveSubLayers()
{
	if(g_hEngine)
	{
		FwpmSubLayerDeleteByKey(g_hEngine, &WFP_SAMPLE_SUBLAYER_GUID);
	}
}

VOID WFPRemoveFilter()
{
	if(g_hEngine)
	{
		FwpmFilterDeleteById0(g_hEngine, g_FilterId);
	}
}

void WFPClassifyFn1(
	const FWPS_INCOMING_VALUES0 *inFixedValues,
	const FWPS_INCOMING_METADATA_VALUES0 *inMetaValues,
	void *layerData,
	const void *classifyContext,
	const FWPS_FILTER1 *filter,
	UINT64 flowContext,
	FWPS_CLASSIFY_OUT0 *classifyOut
)
{
	MyKdPrint(("Enter WFPClassifyFn1\n"));

	classifyOut->actionType = FWP_ACTION_PERMIT;
}

NTSTATUS WFPNotifyFn1(
	FWPS_CALLOUT_NOTIFY_TYPE notifyType,
	const GUID *filterKey,
	FWPS_FILTER1 *filter
)
{
	MyKdPrint(("Enter WFPNotifyFn1\n"));
	return STATUS_SUCCESS;
}

VOID NTAPI
WFPFlowDeleteFn(
	IN UINT16  layerId,
	IN UINT32  calloutId,
	IN UINT64  flowContext
)
{
	MyKdPrint(("Enter WFPFlowDeleteFn\n"));
}


#ifdef __cplusplus
}
#endif
