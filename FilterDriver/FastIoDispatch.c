#include <ntifs.h>
#include "../Monitor/ControlCode.h"
#include "FilterDriver.h"
#include "unordered_map.h"

#define SHOWFASTIO false

extern GLOBAL Global;
IsPE IsPe(IMAGE_DOS_HEADER *header,ULONG Length);
void ReadHeader(DEVICE_OBJECT *DeviceObject,FILE_OBJECT *FileObject,USERDATA *UserData);
bool InsertRecord(Record *record,int drive,UNICODE_STRING *FileName,OperateType operate,long long offset,unsigned long length,IsPE isPE);

void ShowName(const char *name)
{
#if SHOWFASTIO!=0
	KdPrintEx(0,0,"%s\n",name);
#endif
}

DEVICE_OBJECT * FindFastIoDevice(DEVICE_OBJECT *Current,DEVICE_OBJECT *Target)
{
	DEVICE_OBJECT *FastIoDevice=NULL;
	if(Target)
		while(Target!=Current)
		{
			if(Target->DriverObject->FastIoDispatch)
				FastIoDevice=Target;
			Target=Target->AttachedDevice;
		}
	return FastIoDevice;
}

BOOLEAN FastIoCheckIfPossible
(
	_In_ struct _FILE_OBJECT *FileObject,
	_In_ PLARGE_INTEGER FileOffset,
	_In_ ULONG Length,
	_In_ BOOLEAN Wait,
	_In_ ULONG LockKey,
	_In_ BOOLEAN CheckForReadOperation,
	_Pre_notnull_
	_When_(return!=FALSE,_Post_equal_to_(_Old_(IoStatus)))
	_When_(return==FALSE,_Post_valid_)
	PIO_STATUS_BLOCK IoStatus,
	_In_ struct _DEVICE_OBJECT *DeviceObject
)
{
	ShowName(__func__);
	DEVICEEXTENSION *DeviceExtension=(DEVICEEXTENSION*)DeviceObject->DeviceExtension;
	DeviceObject=FindFastIoDevice(DeviceObject,DeviceExtension->Target);
	if(DeviceObject)
	{
		return DeviceObject->DriverObject->FastIoDispatch->
			FastIoCheckIfPossible(FileObject,FileOffset,Length,Wait,LockKey,CheckForReadOperation,IoStatus,DeviceObject);
	}
	return false;
}

BOOLEAN FastIoRead
(
	_In_ struct _FILE_OBJECT *FileObject,
	_In_ PLARGE_INTEGER FileOffset,
	_In_ ULONG Length,
	_In_ BOOLEAN Wait,
	_In_ ULONG LockKey,
	_Out_ PVOID Buffer,
	_Out_ PIO_STATUS_BLOCK IoStatus,
	_In_ struct _DEVICE_OBJECT *DeviceObject
)
{
	bool result=false;
	ShowName(__func__);
	DEVICEEXTENSION *DeviceExtension=(DEVICEEXTENSION*)DeviceObject->DeviceExtension;
	DeviceObject=FindFastIoDevice(DeviceObject,DeviceExtension->Target);
	if(DeviceObject)
	{
		result=DeviceObject->DriverObject->FastIoDispatch->
			FastIoRead(FileObject,FileOffset,Length,Wait,LockKey,Buffer,IoStatus,DeviceObject);
		/*if(FileObject->FileName.Length&&wcscmp(FileObject->FileName.Buffer,L"\\Users\\huanmie\\Desktop\\1.txt")==0)
		{
			int i=0;
		}*/
		KIRQL Irql;
		KeAcquireSpinLock(&Global.wait_lock,&Irql);
		if(DeviceExtension->IsControl==false&&result&&Global.pass)
		{
			InterlockedIncrement(&Global.wait_number);
			KeReleaseSpinLock(&Global.wait_lock,Irql);
			KdPrintEx(0,0,"Request Process:%d\nRequest Mode:%s\nRequest Path:%c:%wZ\n",PsGetCurrentProcessId(),"Read",
				DeviceExtension->Drive+'C',&FileObject->FileName);
			KeAcquireSpinLock(&Global.map_lock,&Irql);
			USERDATA *UserData=Global.map->Get(Global.map,(unsigned long long)FileObject/sizeof(FILE_OBJECT));
			KeReleaseSpinLock(&Global.map_lock,Irql);
			if(UserData)
			{
				if(UserData->ispe==unknown)
				{
					if(Length>sizeof(IMAGE_DOS_HEADER))
						UserData->ispe=IsPe(Buffer,Length);
					else
						ReadHeader(DeviceObject,FileObject,UserData);
				}
				KeAcquireSpinLock(&Global.write_lock,&Irql);
				if(InsertRecord(&Global.head[Global.index],DeviceExtension->Drive,&FileObject->FileName,Read,FileOffset->QuadPart,Length,
					UserData->ispe)&&++Global.index==QueueLength)
					Global.index=0;
				KeReleaseSpinLock(&Global.write_lock,Irql);

			}
			InterlockedDecrement(&Global.wait_number);
		}
		else
			KeReleaseSpinLock(&Global.wait_lock,Irql);
	}
	return result;
}

BOOLEAN FastIoWrite
(
	_In_ struct _FILE_OBJECT *FileObject,
	_In_ PLARGE_INTEGER FileOffset,
	_In_ ULONG Length,
	_In_ BOOLEAN Wait,
	_In_ ULONG LockKey,
	_In_ PVOID Buffer,
	_Out_ PIO_STATUS_BLOCK IoStatus,
	_In_ struct _DEVICE_OBJECT *DeviceObject
)
{
	bool result=false;
	ShowName(__func__);
	DEVICEEXTENSION *DeviceExtension=(DEVICEEXTENSION*)DeviceObject->DeviceExtension;
	DeviceObject=FindFastIoDevice(DeviceObject,DeviceExtension->Target);
	if(DeviceObject)
	{
		KIRQL Irql;
		KeAcquireSpinLock(&Global.wait_lock,&Irql);
		if(DeviceExtension->IsControl==false&&Global.pass)
		{
			InterlockedIncrement(&Global.wait_number);
			KeReleaseSpinLock(&Global.wait_lock,Irql);
			KeAcquireSpinLock(&Global.map_lock,&Irql);
			USERDATA *UserData=Global.map->Get(Global.map,(unsigned long long)FileObject/sizeof(FILE_OBJECT));
			KeReleaseSpinLock(&Global.map_lock,Irql);
			if(UserData)
			{
				if(UserData->ispe==unknown)
				ReadHeader(DeviceObject,FileObject,UserData);
				if(UserData->ispe==pe)
				{
					KeAcquireSpinLock(&Global.query_lock,&Irql);
					Query *query=NULL;
					for(int i=0;i<QueryQueueLength;++i)
						if(Global.query[i].record.complete==false)
						{
							query=&Global.query[i];
							break;
						}
					KeReleaseSpinLock(&Global.query_lock,Irql);
					if(query)
					{
						MDL *mdl=IoAllocateMdl(Buffer,Length,false,false,false);
						MmProbeAndLockPages(mdl,KernelMode,IoReadAccess);
						KAPC_STATE apc_state;
						KeStackAttachProcess(Global.process,&apc_state);
						query->mapped=MmMapLockedPagesSpecifyCache(mdl,UserMode,MmCached,NULL,false,NormalPagePriority);
						KeUnstackDetachProcess(&apc_state);
						query->allow=allow_unknown;
						query->waiting=false;
						InsertRecord(&query->record,DeviceExtension->Drive,&FileObject->FileName,Write,FileOffset->QuadPart,Length,UserData->ispe);
						while(query->allow==allow_unknown);
						KeStackAttachProcess(Global.process,&apc_state);
						MmUnmapLockedPages(query->mapped,mdl);
						KeUnstackDetachProcess(&apc_state);
						MmUnlockPages(mdl);
						IoFreeMdl(mdl);
						if(query->allow==allow_true)
						{
							result=DeviceObject->DriverObject->FastIoDispatch->
								FastIoWrite(FileObject,FileOffset,Length,Wait,LockKey,Buffer,IoStatus,DeviceObject);
							if(result)
							{
								KdPrintEx(0,0,"Request Process:%d\nRequest Mode:%s\nRequest Path:%c:%wZ\n",PsGetCurrentProcessId(),"Write",
									DeviceExtension->Drive+'C',&FileObject->FileName);
								query->allow=allow_success;
							}
							else
								query->allow=allow_fail;
						}
						else
							result=false;
					}
					else
						DbgPrintEx(0,0,"Insufficient memory in circular linked query list.\n");
				}
				else
				{
					result=DeviceObject->DriverObject->FastIoDispatch->
						FastIoWrite(FileObject,FileOffset,Length,Wait,LockKey,Buffer,IoStatus,DeviceObject);
					if(result)
					{
						KdPrintEx(0,0,"Request Process:%d\nRequest Mode:%s\nRequest Path:%c:%wZ\n",PsGetCurrentProcessId(),"Write",
							DeviceExtension->Drive+'C',&FileObject->FileName);
						KeAcquireSpinLock(&Global.write_lock,&Irql);
						if(InsertRecord(&Global.head[Global.index],DeviceExtension->Drive,&FileObject->FileName,Write,
							FileOffset->QuadPart,Length,UserData->ispe)&&++Global.index==QueueLength)
							Global.index=0;
						KeReleaseSpinLock(&Global.write_lock,Irql);
					}
				}
			}
			InterlockedDecrement(&Global.wait_number);
		}
		else
			KeReleaseSpinLock(&Global.wait_lock,Irql);
	}
	return result;
}

BOOLEAN FastIoQueryBasicInfo
(
	_In_ struct _FILE_OBJECT *FileObject,
	_In_ BOOLEAN Wait,
	_Out_ PFILE_BASIC_INFORMATION Buffer,
	_Out_ PIO_STATUS_BLOCK IoStatus,
	_In_ struct _DEVICE_OBJECT *DeviceObject
)
{
	ShowName(__func__);
	DEVICEEXTENSION *DeviceExtension=(DEVICEEXTENSION*)DeviceObject->DeviceExtension;
	DeviceObject=FindFastIoDevice(DeviceObject,DeviceExtension->Target);
	if(DeviceObject)
	{
		return DeviceObject->DriverObject->FastIoDispatch->
			FastIoQueryBasicInfo(FileObject,Wait,Buffer,IoStatus,DeviceObject);
	}
	return false;
}

BOOLEAN FastIoQueryStandardInfo
(
	_In_ struct _FILE_OBJECT *FileObject,
	_In_ BOOLEAN Wait,
	_Out_ PFILE_STANDARD_INFORMATION Buffer,
	_Out_ PIO_STATUS_BLOCK IoStatus,
	_In_ struct _DEVICE_OBJECT *DeviceObject
)
{
	ShowName(__func__);
	DEVICEEXTENSION *DeviceExtension=(DEVICEEXTENSION*)DeviceObject->DeviceExtension;
	DeviceObject=FindFastIoDevice(DeviceObject,DeviceExtension->Target);
	if(DeviceObject)
	{
		return DeviceObject->DriverObject->FastIoDispatch->
			FastIoQueryStandardInfo(FileObject,Wait,Buffer,IoStatus,DeviceObject);
	}
	return false;
}

BOOLEAN FastIoLock
(
	_In_ struct _FILE_OBJECT *FileObject,
	_In_ PLARGE_INTEGER FileOffset,
	_In_ PLARGE_INTEGER Length,
	_In_ PEPROCESS ProcessId,
	_In_ ULONG Key,
	_In_ BOOLEAN FailImmediately,
	_In_ BOOLEAN ExclusiveLock,
	_Out_ PIO_STATUS_BLOCK IoStatus,
	_In_ struct _DEVICE_OBJECT *DeviceObject
)
{
	ShowName(__func__);
	DEVICEEXTENSION *DeviceExtension=(DEVICEEXTENSION*)DeviceObject->DeviceExtension;
	DeviceObject=FindFastIoDevice(DeviceObject,DeviceExtension->Target);
	if(DeviceObject)
	{
		return DeviceObject->DriverObject->FastIoDispatch->
			FastIoLock(FileObject,FileOffset,Length,ProcessId,Key,FailImmediately,ExclusiveLock,IoStatus,DeviceObject);
	}
	return false;
}

BOOLEAN FastIoUnlockSingle
(
	_In_ struct _FILE_OBJECT *FileObject,
	_In_ PLARGE_INTEGER FileOffset,
	_In_ PLARGE_INTEGER Length,
	_In_ PEPROCESS ProcessId,
	_In_ ULONG Key,
	_Out_ PIO_STATUS_BLOCK IoStatus,
	_In_ struct _DEVICE_OBJECT *DeviceObject
)
{
	ShowName(__func__);
	DEVICEEXTENSION *DeviceExtension=(DEVICEEXTENSION*)DeviceObject->DeviceExtension;
	DeviceObject=FindFastIoDevice(DeviceObject,DeviceExtension->Target);
	if(DeviceObject)
	{
		return DeviceObject->DriverObject->FastIoDispatch->
			FastIoUnlockSingle(FileObject,FileOffset,Length,ProcessId,Key,IoStatus,DeviceObject);
	}
	return false;
}

BOOLEAN FastIoUnlockAll
(
	_In_ struct _FILE_OBJECT *FileObject,
	_In_ PEPROCESS ProcessId,
	_Out_ PIO_STATUS_BLOCK IoStatus,
	_In_ struct _DEVICE_OBJECT *DeviceObject
)
{
	ShowName(__func__);
	DEVICEEXTENSION *DeviceExtension=(DEVICEEXTENSION*)DeviceObject->DeviceExtension;
	DeviceObject=FindFastIoDevice(DeviceObject,DeviceExtension->Target);
	if(DeviceObject)
	{
		return DeviceObject->DriverObject->FastIoDispatch->
			FastIoUnlockAll(FileObject,ProcessId,IoStatus,DeviceObject);
	}
	return false;
}

BOOLEAN FastIoUnlockAllByKey
(
	_In_ struct _FILE_OBJECT *FileObject,
	_In_ PVOID ProcessId,
	_In_ ULONG Key,
	_Out_ PIO_STATUS_BLOCK IoStatus,
	_In_ struct _DEVICE_OBJECT *DeviceObject
)
{
	ShowName(__func__);
	DEVICEEXTENSION *DeviceExtension=(DEVICEEXTENSION*)DeviceObject->DeviceExtension;
	DeviceObject=FindFastIoDevice(DeviceObject,DeviceExtension->Target);
	if(DeviceObject)
	{
		return DeviceObject->DriverObject->FastIoDispatch->
			FastIoUnlockAllByKey(FileObject,ProcessId,Key,IoStatus,DeviceObject);
	}
	return false;
}

BOOLEAN FastIoDeviceControl
(
	_In_ struct _FILE_OBJECT *FileObject,
	_In_ BOOLEAN Wait,
	_In_opt_ PVOID InputBuffer,
	_In_ ULONG InputBufferLength,
	_Out_opt_ PVOID OutputBuffer,
	_In_ ULONG OutputBufferLength,
	_In_ ULONG IoControlCode,
	_Out_ PIO_STATUS_BLOCK IoStatus,
	_In_ struct _DEVICE_OBJECT *DeviceObject
)
{
	ShowName(__func__);
	DEVICEEXTENSION *DeviceExtension=(DEVICEEXTENSION*)DeviceObject->DeviceExtension;
	DeviceObject=FindFastIoDevice(DeviceObject,DeviceExtension->Target);
	if(DeviceObject)
	{
		return DeviceObject->DriverObject->FastIoDispatch->
			FastIoDeviceControl(FileObject,Wait,InputBuffer,InputBufferLength,OutputBuffer,OutputBufferLength,
				IoControlCode,IoStatus,DeviceObject);
	}
	return false;
}

VOID AcquireFileForNtCreateSection
(
	_In_ struct _FILE_OBJECT *FileObject
)
{
	ShowName(__func__);
	DEVICEEXTENSION *DeviceExtension=(DEVICEEXTENSION*)FileObject->DeviceObject->DeviceExtension;
	DEVICE_OBJECT *DeviceObject=FindFastIoDevice(FileObject->DeviceObject,DeviceExtension->Target);
	if(DeviceObject)
		DeviceObject->DriverObject->FastIoDispatch->AcquireFileForNtCreateSection(FileObject);
}

VOID ReleaseFileForNtCreateSection
(
	_In_ struct _FILE_OBJECT *FileObject
)
{
	ShowName(__func__);
	DEVICEEXTENSION *DeviceExtension=(DEVICEEXTENSION*)FileObject->DeviceObject->DeviceExtension;
	DEVICE_OBJECT *DeviceObject=FindFastIoDevice(FileObject->DeviceObject,DeviceExtension->Target);
	if(DeviceObject)
		DeviceObject->DriverObject->FastIoDispatch->ReleaseFileForNtCreateSection(FileObject);
}

VOID FastIoDetachDevice
(
	_In_ struct _DEVICE_OBJECT *SourceDevice,
	_In_ struct _DEVICE_OBJECT *TargetDevice
)
{
	ShowName(__func__);
	IoDetachDevice(TargetDevice);
	IoDeleteDevice(SourceDevice);
}

BOOLEAN FastIoQueryNetworkOpenInfo
(
	_In_ struct _FILE_OBJECT *FileObject,
	_In_ BOOLEAN Wait,
	_Out_ struct _FILE_NETWORK_OPEN_INFORMATION *Buffer,
	_Out_ struct _IO_STATUS_BLOCK *IoStatus,
	_In_ struct _DEVICE_OBJECT *DeviceObject
)
{
	ShowName(__func__);
	DEVICEEXTENSION *DeviceExtension=(DEVICEEXTENSION*)DeviceObject->DeviceExtension;
	DeviceObject=FindFastIoDevice(DeviceObject,DeviceExtension->Target);
	if(DeviceObject)
	{
		return DeviceObject->DriverObject->FastIoDispatch->
			FastIoQueryNetworkOpenInfo(FileObject,Wait,Buffer,IoStatus,DeviceObject);
	}
	return false;
}

NTSTATUS AcquireForModWrite
(
	_In_ struct _FILE_OBJECT *FileObject,
	_In_ PLARGE_INTEGER EndingOffset,
	_Out_ struct _ERESOURCE **ResourceToRelease,
	_In_ struct _DEVICE_OBJECT *DeviceObject
)
{
	ShowName(__func__);
	DEVICEEXTENSION *DeviceExtension=(DEVICEEXTENSION*)DeviceObject->DeviceExtension;
	DeviceObject=FindFastIoDevice(DeviceObject,DeviceExtension->Target);
	if(DeviceObject)
	{
		return DeviceObject->DriverObject->FastIoDispatch->
			AcquireForModWrite(FileObject,EndingOffset,ResourceToRelease,DeviceObject);
	}
	return STATUS_UNSUCCESSFUL;
}

BOOLEAN MdlRead
(
	_In_ struct _FILE_OBJECT *FileObject,
	_In_ PLARGE_INTEGER FileOffset,
	_In_ ULONG Length,
	_In_ ULONG LockKey,
	_Out_ PMDL *MdlChain,
	_Out_ PIO_STATUS_BLOCK IoStatus,
	_In_ struct _DEVICE_OBJECT *DeviceObject
)
{
	ShowName(__func__);
	DEVICEEXTENSION *DeviceExtension=(DEVICEEXTENSION*)DeviceObject->DeviceExtension;
	DeviceObject=FindFastIoDevice(DeviceObject,DeviceExtension->Target);
	if(DeviceObject)
	{
		return DeviceObject->DriverObject->FastIoDispatch->
			MdlRead(FileObject,FileOffset,Length,LockKey,MdlChain,IoStatus,DeviceObject);
	}
	return false;
}

BOOLEAN MdlReadComplete
(
	_In_ struct _FILE_OBJECT *FileObject,
	_In_ PMDL MdlChain,
	_In_ struct _DEVICE_OBJECT *DeviceObject
)
{
	ShowName(__func__);
	DEVICEEXTENSION *DeviceExtension=(DEVICEEXTENSION*)DeviceObject->DeviceExtension;
	DeviceObject=FindFastIoDevice(DeviceObject,DeviceExtension->Target);
	if(DeviceObject)
	{
		return DeviceObject->DriverObject->FastIoDispatch->
			MdlReadComplete(FileObject,MdlChain,DeviceObject);
	}
	return false;
}

BOOLEAN PrepareMdlWrite
(
	_In_ struct _FILE_OBJECT *FileObject,
	_In_ PLARGE_INTEGER FileOffset,
	_In_ ULONG Length,
	_In_ ULONG LockKey,
	_Out_ PMDL *MdlChain,
	_Out_ PIO_STATUS_BLOCK IoStatus,
	_In_ struct _DEVICE_OBJECT *DeviceObject
)
{
	ShowName(__func__);
	DEVICEEXTENSION *DeviceExtension=(DEVICEEXTENSION*)DeviceObject->DeviceExtension;
	DeviceObject=FindFastIoDevice(DeviceObject,DeviceExtension->Target);
	if(DeviceObject)
	{
		return DeviceObject->DriverObject->FastIoDispatch->
			PrepareMdlWrite(FileObject,FileOffset,Length,LockKey,MdlChain,IoStatus,DeviceObject);
	}
	return false;
}

BOOLEAN MdlWriteComplete
(
	_In_ struct _FILE_OBJECT *FileObject,
	_In_ PLARGE_INTEGER FileOffset,
	_In_ PMDL MdlChain,
	_In_ struct _DEVICE_OBJECT *DeviceObject
)
{
	ShowName(__func__);
	DEVICEEXTENSION *DeviceExtension=(DEVICEEXTENSION*)DeviceObject->DeviceExtension;
	DeviceObject=FindFastIoDevice(DeviceObject,DeviceExtension->Target);
	if(DeviceObject)
	{
		return DeviceObject->DriverObject->FastIoDispatch->
			MdlWriteComplete(FileObject,FileOffset,MdlChain,DeviceObject);
	}
	return false;
}

BOOLEAN FastIoReadCompressed
(
	_In_ struct _FILE_OBJECT *FileObject,
	_In_ PLARGE_INTEGER FileOffset,
	_In_ ULONG Length,
	_In_ ULONG LockKey,
	_Out_ PVOID Buffer,
	_Out_ PMDL *MdlChain,
	_Out_ PIO_STATUS_BLOCK IoStatus,
	_Out_ struct _COMPRESSED_DATA_INFO *CompressedDataInfo,
	_In_ ULONG CompressedDataInfoLength,
	_In_ struct _DEVICE_OBJECT *DeviceObject
)
{
	ShowName(__func__);
	DEVICEEXTENSION *DeviceExtension=(DEVICEEXTENSION*)DeviceObject->DeviceExtension;
	DeviceObject=FindFastIoDevice(DeviceObject,DeviceExtension->Target);
	if(DeviceObject)
	{
		return DeviceObject->DriverObject->FastIoDispatch->
			FastIoReadCompressed(FileObject,FileOffset,Length,LockKey,Buffer,MdlChain,IoStatus,
				CompressedDataInfo,CompressedDataInfoLength,DeviceObject);
	}
	return false;
}

BOOLEAN FastIoWriteCompressed
(
	_In_ struct _FILE_OBJECT *FileObject,
	_In_ PLARGE_INTEGER FileOffset,
	_In_ ULONG Length,
	_In_ ULONG LockKey,
	_In_ PVOID Buffer,
	_Out_ PMDL *MdlChain,
	_Out_ PIO_STATUS_BLOCK IoStatus,
	_In_ struct _COMPRESSED_DATA_INFO *CompressedDataInfo,
	_In_ ULONG CompressedDataInfoLength,
	_In_ struct _DEVICE_OBJECT *DeviceObject
)
{
	ShowName(__func__);
	DEVICEEXTENSION *DeviceExtension=(DEVICEEXTENSION*)DeviceObject->DeviceExtension;
	DeviceObject=FindFastIoDevice(DeviceObject,DeviceExtension->Target);
	if(DeviceObject)
	{
		return DeviceObject->DriverObject->FastIoDispatch->
			FastIoWriteCompressed(FileObject,FileOffset,Length,LockKey,Buffer,MdlChain,IoStatus,
				CompressedDataInfo,CompressedDataInfoLength,DeviceObject);
	}
	return false;
}

BOOLEAN MdlReadCompleteCompressed
(
	_In_ struct _FILE_OBJECT *FileObject,
	_In_ PMDL MdlChain,
	_In_ struct _DEVICE_OBJECT *DeviceObject
)
{
	ShowName(__func__);
	DEVICEEXTENSION *DeviceExtension=(DEVICEEXTENSION*)DeviceObject->DeviceExtension;
	DeviceObject=FindFastIoDevice(DeviceObject,DeviceExtension->Target);
	if(DeviceObject)
	{
		return DeviceObject->DriverObject->FastIoDispatch->
			MdlReadCompleteCompressed(FileObject,MdlChain,DeviceObject);
	}
	return false;
}

BOOLEAN MdlWriteCompleteCompressed
(
	_In_ struct _FILE_OBJECT *FileObject,
	_In_ PLARGE_INTEGER FileOffset,
	_In_ PMDL MdlChain,
	_In_ struct _DEVICE_OBJECT *DeviceObject
)
{
	ShowName(__func__);
	DEVICEEXTENSION *DeviceExtension=(DEVICEEXTENSION*)DeviceObject->DeviceExtension;
	DeviceObject=FindFastIoDevice(DeviceObject,DeviceExtension->Target);
	if(DeviceObject)
	{
		return DeviceObject->DriverObject->FastIoDispatch->
			MdlWriteCompleteCompressed(FileObject,FileOffset,MdlChain,DeviceObject);
	}
	return false;
}

BOOLEAN FastIoQueryOpen
(
	_Inout_ struct _IRP *Irp,
	_Out_ PFILE_NETWORK_OPEN_INFORMATION NetworkInformation,
	_In_ struct _DEVICE_OBJECT *DeviceObject
)
{
	ShowName(__func__);
	DEVICEEXTENSION *DeviceExtension=(DEVICEEXTENSION*)DeviceObject->DeviceExtension;
	DeviceObject=FindFastIoDevice(DeviceObject,DeviceExtension->Target);
	if(DeviceObject)
	{
		return DeviceObject->DriverObject->FastIoDispatch->
			FastIoQueryOpen(Irp,NetworkInformation,DeviceObject);
	}
	return false;
}

NTSTATUS ReleaseForModWrite
(
	_In_ struct _FILE_OBJECT *FileObject,
	_In_ struct _ERESOURCE *ResourceToRelease,
	_In_ struct _DEVICE_OBJECT *DeviceObject
)
{
	ShowName(__func__);
	DEVICEEXTENSION *DeviceExtension=(DEVICEEXTENSION*)DeviceObject->DeviceExtension;
	DeviceObject=FindFastIoDevice(DeviceObject,DeviceExtension->Target);
	if(DeviceObject)
	{
		return DeviceObject->DriverObject->FastIoDispatch->
			ReleaseForModWrite(FileObject,ResourceToRelease,DeviceObject);
	}
	return STATUS_UNSUCCESSFUL;
}

NTSTATUS AcquireForCcFlush
(
	_In_ struct _FILE_OBJECT *FileObject,
	_In_ struct _DEVICE_OBJECT *DeviceObject
)
{
	ShowName(__func__);
	DEVICEEXTENSION *DeviceExtension=(DEVICEEXTENSION*)DeviceObject->DeviceExtension;
	DeviceObject=FindFastIoDevice(DeviceObject,DeviceExtension->Target);
	if(DeviceObject)
	{
		return DeviceObject->DriverObject->FastIoDispatch->
			AcquireForCcFlush(FileObject,DeviceObject);
	}
	return STATUS_UNSUCCESSFUL;
}

NTSTATUS ReleaseForCcFlush
(
	_In_ struct _FILE_OBJECT *FileObject,
	_In_ struct _DEVICE_OBJECT *DeviceObject
)
{
	ShowName(__func__);
	DEVICEEXTENSION *DeviceExtension=(DEVICEEXTENSION*)DeviceObject->DeviceExtension;
	DeviceObject=FindFastIoDevice(DeviceObject,DeviceExtension->Target);
	if(DeviceObject)
	{
		return DeviceObject->DriverObject->FastIoDispatch->
			ReleaseForCcFlush(FileObject,DeviceObject);
	}
	return STATUS_UNSUCCESSFUL;
}

void InitializeFastIoDispatch(FAST_IO_DISPATCH *FastIoDispatch)
{
	memset(FastIoDispatch,0,sizeof(FAST_IO_DISPATCH));
	FastIoDispatch->SizeOfFastIoDispatch=sizeof(*FastIoDispatch);
	FastIoDispatch->FastIoCheckIfPossible=FastIoCheckIfPossible;
	FastIoDispatch->FastIoRead=FastIoRead;
	FastIoDispatch->FastIoWrite=FastIoWrite;
	FastIoDispatch->FastIoQueryBasicInfo=FastIoQueryBasicInfo;
	FastIoDispatch->FastIoQueryStandardInfo=FastIoQueryStandardInfo;
	FastIoDispatch->FastIoLock=FastIoLock;
	FastIoDispatch->FastIoUnlockSingle=FastIoUnlockSingle;
	FastIoDispatch->FastIoUnlockAll=FastIoUnlockAll;
	FastIoDispatch->FastIoUnlockAllByKey=FastIoUnlockAllByKey;
	FastIoDispatch->FastIoDeviceControl=FastIoDeviceControl;
	FastIoDispatch->AcquireFileForNtCreateSection=AcquireFileForNtCreateSection;
	FastIoDispatch->ReleaseFileForNtCreateSection=ReleaseFileForNtCreateSection;
	FastIoDispatch->FastIoDetachDevice=FastIoDetachDevice;
	FastIoDispatch->FastIoQueryNetworkOpenInfo=FastIoQueryNetworkOpenInfo;
	FastIoDispatch->AcquireForModWrite=AcquireForModWrite;
	FastIoDispatch->MdlRead=MdlRead;
	FastIoDispatch->MdlReadComplete=MdlReadComplete;
	FastIoDispatch->PrepareMdlWrite=PrepareMdlWrite;
	FastIoDispatch->MdlWriteComplete=MdlWriteComplete;
	FastIoDispatch->FastIoReadCompressed=FastIoReadCompressed;
	FastIoDispatch->FastIoWriteCompressed=FastIoWriteCompressed;
	FastIoDispatch->MdlReadCompleteCompressed=MdlReadCompleteCompressed;
	FastIoDispatch->MdlWriteCompleteCompressed=MdlWriteCompleteCompressed;
	FastIoDispatch->FastIoQueryOpen=FastIoQueryOpen;
	FastIoDispatch->ReleaseForModWrite=ReleaseForModWrite;
	FastIoDispatch->AcquireForCcFlush=AcquireForCcFlush;
	FastIoDispatch->ReleaseForCcFlush=ReleaseForCcFlush;
}