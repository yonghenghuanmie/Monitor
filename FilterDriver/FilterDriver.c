//for KeInitializeSpinLock on win7
#ifdef NTDDI_VERSION
#undef NTDDI_VERSION
#endif // NTDDI_VERSION
#define NTDDI_VERSION NTDDI_WIN7

#include <ntifs.h>
#include "../Monitor/ControlCode.h"
#include "FilterDriver.h"
#include "unordered_map.h"

GLOBAL Global;
const int header_size=0x150;

DEVICE_OBJECT * AlreadyAttached(DRIVER_OBJECT *DriverObject,DEVICE_OBJECT *Target)
{
	DEVICE_OBJECT *DeviceObject=DriverObject->DeviceObject;
	while(DeviceObject)
	{
		DEVICEEXTENSION *DeviceExtension=(DEVICEEXTENSION*)DeviceObject->DeviceExtension;
		if(DeviceExtension&&DeviceExtension->Target==Target)
			return DeviceObject;
		DeviceObject=DeviceObject->NextDevice;
	}
	return NULL;
}

bool HasObjectName(void *object)
{
	bool HasName=false;
	ULONG ReturnLength;
	ObQueryNameString(object,NULL,0,&ReturnLength);
	OBJECT_NAME_INFORMATION *ObjectNameInformation=(OBJECT_NAME_INFORMATION*)ExAllocatePool(PagedPool,ReturnLength);
	ASSERT(ObjectNameInformation);
	ASSERT(NT_SUCCESS(ObQueryNameString(object,ObjectNameInformation,ReturnLength,&ReturnLength)));
	if(ObjectNameInformation->Name.Buffer)
	{
		KdPrintEx(0,0,"%wZ\n",&ObjectNameInformation->Name);
		HasName=true;
	}
	ExFreePool(ObjectNameInformation);
	return HasName;
}

DRIVER_OBJECT * GetDriverObjectByName(UNICODE_STRING *DriverName)
{
	DRIVER_OBJECT *DriverObject=NULL;
	ObReferenceObjectByName(DriverName,0,NULL,FILE_ALL_ACCESS,*IoDriverObjectType,KernelMode,NULL,&DriverObject);
	return DriverObject;
}

NTSTATUS GetDeviceNameBySymbolicLink(IN OUT UNICODE_STRING *DeviceName)
{
	HANDLE hSymbolicLink;
	OBJECT_ATTRIBUTES ObjectAttributes;
	InitializeObjectAttributes(&ObjectAttributes,DeviceName,0,NULL,NULL);
	NTSTATUS status=ZwOpenSymbolicLinkObject(&hSymbolicLink,GENERIC_READ,&ObjectAttributes);
	if(NT_SUCCESS(status))
		status=ZwQuerySymbolicLinkObject(hSymbolicLink,DeviceName,NULL);
	return status;
}

DEVICE_OBJECT * GetFileSystemDeviceByName(UNICODE_STRING *ObjectName)
{
	DEVICE_OBJECT *DeviceObject=NULL;
	HANDLE hFile;
	OBJECT_ATTRIBUTES ObjectAttributes;
	InitializeObjectAttributes(&ObjectAttributes,ObjectName,0,NULL,NULL);
	IO_STATUS_BLOCK IoStatusBlock;
	NTSTATUS status=ZwCreateFile(&hFile,0,&ObjectAttributes,&IoStatusBlock,NULL,
		0,FILE_SHARE_READ,FILE_OPEN,FILE_SYNCHRONOUS_IO_NONALERT,NULL,0);
	if(NT_SUCCESS(status))
	{
		FILE_OBJECT *FileObject;
		status=ObReferenceObjectByHandle(hFile,FILE_ALL_ACCESS,*IoFileObjectType,KernelMode,&FileObject,NULL);
		if(NT_SUCCESS(status))
		{
			if(FileObject->DeviceObject&&FileObject->DeviceObject->Vpb)
				DeviceObject=FileObject->DeviceObject->Vpb->DeviceObject;
			ObDereferenceObject(FileObject);
		}
		ZwClose(hFile);
	}
	return DeviceObject;
}

DEVICE_OBJECT * Attach(DRIVER_OBJECT *DriverObject,DEVICE_OBJECT *Target)
{
	DEVICE_OBJECT *DeviceObject=NULL;
	if(NT_SUCCESS(IoCreateDevice(DriverObject,sizeof(DEVICEEXTENSION),NULL,FILE_DEVICE_FILE_SYSTEM,0,FALSE,&DeviceObject)))
	{
		DEVICEEXTENSION *DeviceExtension=(DEVICEEXTENSION*)DeviceObject->DeviceExtension;
		DeviceExtension->Target=Target;
		DeviceExtension->Next=IoAttachDeviceToDeviceStack(DeviceObject,DeviceExtension->Target);
	#ifdef DBG
		DeviceExtension->Global=&Global;
	#endif // DBG
		DeviceObject->Type=DeviceExtension->Next->Type;
		DeviceObject->Characteristics=DeviceExtension->Next->Characteristics;
		DeviceObject->Flags&=~DO_DEVICE_INITIALIZING;
		DeviceObject->Flags|=DeviceExtension->Next->Flags&(DO_DIRECT_IO|DO_BUFFERED_IO);
	}
	return DeviceObject;
}

void AttachToVolume(DRIVER_OBJECT *DriverObject)
{
	UNICODE_STRING ObjectName;
	ObjectName.MaximumLength=128;
	ObjectName.Buffer=(wchar_t*)ExAllocatePool(PagedPool,ObjectName.MaximumLength);
	ASSERT(ObjectName.Buffer);
	int i;
	for(i=0;i<24;i++)
	{
		wcscpy(ObjectName.Buffer,L"\\??\\C:");
		ObjectName.Buffer[4]=(wchar_t)('C'+i);
		ObjectName.Length=(unsigned short)(wcslen(ObjectName.Buffer)*sizeof(wchar_t));
		NTSTATUS status=GetDeviceNameBySymbolicLink(&ObjectName);
		if(NT_SUCCESS(status))
		{
			DEVICE_OBJECT *DeviceObject=GetFileSystemDeviceByName(&ObjectName);
			if(DeviceObject)
			{
				DeviceObject=Attach(DriverObject,DeviceObject);
				if(DeviceObject)
				{
					DEVICEEXTENSION *DeviceExtension=(DEVICEEXTENSION*)DeviceObject->DeviceExtension;
					DeviceExtension->Drive=i;
					Global.fs_count++;
				}
			}
		}
	}
	ExFreePool(ObjectName.Buffer);
}

void FileSystemChange(_In_ struct _DEVICE_OBJECT *DeviceObject,_In_ BOOLEAN FsActive)
{
	if(FsActive)
	{
		UNICODE_STRING DriverName;
		RtlInitUnicodeString(&DriverName,L"\\FileSystem\\Ntfs");
		DRIVER_OBJECT *Ntfs=GetDriverObjectByName(&DriverName);
		RtlInitUnicodeString(&DriverName,L"\\FileSystem\\fastfat");
		DRIVER_OBJECT *fastfat=GetDriverObjectByName(&DriverName);

		if((DeviceObject->DriverObject==Ntfs||DeviceObject->DriverObject==fastfat))
		{
			HasObjectName(DeviceObject);
			DeviceObject=Attach(Global.interactive->DriverObject,DeviceObject);
			if(DeviceObject)
			{
				DEVICEEXTENSION *DeviceExtension=(DEVICEEXTENSION*)DeviceObject->DeviceExtension;
				DeviceExtension->IsControl=true;
			}
		}
	}
	else
	{
		DeviceObject=AlreadyAttached(Global.interactive->DriverObject,DeviceObject);
		if(DeviceObject)
		{
			IoDetachDevice(((DEVICEEXTENSION*)DeviceObject->DeviceExtension)->Next);
			IoDeleteDevice(DeviceObject);
		}
	}
}

bool InsertRecord(Record *record,int drive,UNICODE_STRING *FileName,OperateType operate,long long offset,unsigned long length,IsPE isPE)
{
	if(record->complete==false)
	{
		PEB *Peb=PsGetProcessPeb(PsGetCurrentProcess());
		UNICODE_STRING *process_name=NULL;
		if(Peb&&Peb->ProcessParameters)
			process_name=&Peb->ProcessParameters->ImagePathName;
		if(process_name&&FileName->Length)
		{
			KeQuerySystemTime(&record->file_time);
			wcscpy_s(record->process_name,process_name->Length/sizeof(wchar_t)+1,process_name->Buffer);
			record->name[0]=(wchar_t)(drive+'C');
			record->name[1]=L':';
			if(wcscpy_s(record->name+2,FileName->Length/sizeof(wchar_t)+1,FileName->Buffer))
			{
				record->name[2]=FileName->Buffer[0];
				record->name[FileName->Length/sizeof(wchar_t)+2]=0;
			}
			record->operate_type=operate;
			record->offset=offset;
			record->length=length;
			record->isPE=isPE;
			record->complete=true;
			return true;
		}
	}
	else
		DbgPrintEx(0,0,"Insufficient memory in circular linked list.\n");
	return false;
}

NTSTATUS PassThrough(DEVICE_OBJECT *DeviceObject,IRP *Irp)
{
	if(DeviceObject!=Global.interactive)
	{
		DEVICEEXTENSION *DeviceExtension=(DEVICEEXTENSION*)DeviceObject->DeviceExtension;
		IoSkipCurrentIrpStackLocation(Irp);
		return IoCallDriver(DeviceExtension->Next,Irp);
	}
	Irp->IoStatus.Status=STATUS_SUCCESS;
	Irp->IoStatus.Information=0;
	IoCompleteRequest(Irp,IO_NO_INCREMENT);
	return STATUS_SUCCESS;
}

IsPE IsPe(IMAGE_DOS_HEADER *header,ULONG Length)
{
	ASSERT(Length>sizeof(IMAGE_DOS_HEADER));
	//'MZ','PE'
	if(header->e_magic=='ZM')
	{
		if(Length>header->e_lfanew+sizeof(long))
		{
			if(*(unsigned short*)((char*)header+header->e_lfanew)=='EP')
				return pe;
			else
				return notpe;
		}
		else
			return unknown;
	}
	else
		return notpe;
}

void ReadHeader(DEVICE_OBJECT *DeviceObject,FILE_OBJECT *FileObject,USERDATA *UserData)
{
	DEVICEEXTENSION *DeviceExtension=(DEVICEEXTENSION*)DeviceObject->DeviceExtension;
	void *buffer=ExAllocateFromNPagedLookasideList(&Global.lookaside);
	LARGE_INTEGER offset={0};
	KEVENT event;
	KeInitializeEvent(&event,NotificationEvent,false);
	IO_STATUS_BLOCK status;
	IRP *read=IoBuildSynchronousFsdRequest(IRP_MJ_READ,DeviceExtension->Target,buffer,header_size,&offset,&event,&status);
	if(read)
	{
		LARGE_INTEGER backup=FileObject->CurrentByteOffset;
		IO_STACK_LOCATION *stack=IoGetNextIrpStackLocation(read);
		stack->Parameters.Read.Length=header_size;
		stack->FileObject=FileObject;
		if(IoCallDriver(DeviceExtension->Next,read)==STATUS_PENDING)
		KeWaitForSingleObject(&event,Executive,KernelMode,false,NULL);
		FileObject->CurrentByteOffset=backup;
		if(NT_SUCCESS(status.Status))
			UserData->ispe=IsPe((IMAGE_DOS_HEADER*)buffer,header_size);
		else if(status.Status==STATUS_END_OF_FILE)
			UserData->ispe=notpe;
	}
	ExFreeToNPagedLookasideList(&Global.lookaside,buffer);
}

NTSTATUS CreateComplete(DEVICE_OBJECT *DeviceObject,IRP *Irp,PVOID Context)
{
	CREATEFILE *CreateFile=(CREATEFILE*)Context;
	CreateFile->Status=Irp->IoStatus.Status;
	KeSetEvent(&CreateFile->Kevent,IO_NO_INCREMENT,false);
	return STATUS_SUCCESS;
}

NTSTATUS DeviceCreate(DEVICE_OBJECT *DeviceObject,IRP *Irp)
{
	DEVICEEXTENSION *DeviceExtension=(DEVICEEXTENSION*)DeviceObject->DeviceExtension;
	KIRQL Irql;
	KeAcquireSpinLock(&Global.wait_lock,&Irql);
	if(DeviceExtension->IsControl==false&&Global.pass)
	{
		InterlockedIncrement(&Global.wait_number);
		KeReleaseSpinLock(&Global.wait_lock,Irql);
		FILE_OBJECT *FileObject=IoGetCurrentIrpStackLocation(Irp)->FileObject;
		CREATEFILE CreateFile;
		KeInitializeEvent(&CreateFile.Kevent,NotificationEvent,false);
		IoCopyCurrentIrpStackLocationToNext(Irp);
		IoSetCompletionRoutine(Irp,CreateComplete,&CreateFile,true,true,true);
		IoCallDriver(DeviceExtension->Next,Irp);
		KeWaitForSingleObject(&CreateFile.Kevent,Executive,KernelMode,false,NULL);
		if(NT_SUCCESS(CreateFile.Status))
		{
			KeAcquireSpinLock(&Global.map_lock,&Irql);
			if(Global.map->Get(Global.map,(unsigned long long)FileObject/sizeof(FILE_OBJECT))==NULL)
				Global.map->Insert(Global.map,(unsigned long long)FileObject/sizeof(FILE_OBJECT),(USERDATA){ unknown });
			else
				ASSERT(false);
			KeReleaseSpinLock(&Global.map_lock,Irql);
		}
		InterlockedDecrement(&Global.wait_number);
		return CreateFile.Status;
	}
	else
		KeReleaseSpinLock(&Global.wait_lock,Irql);
	return PassThrough(DeviceObject,Irp);
}

NTSTATUS ReadComplete(DEVICE_OBJECT *DeviceObject,IRP *Irp,PVOID Context)
{
	READFILE *ReadFile=(READFILE*)Context;
	ReadFile->Status=Irp->IoStatus.Status;
	if(NT_SUCCESS(Irp->IoStatus.Status))
	{
		KIRQL Irql;
		KeAcquireSpinLock(&Global.map_lock,&Irql);
		USERDATA *UserData=Global.map->Get(Global.map,(unsigned long long)ReadFile->FileObject/sizeof(FILE_OBJECT));
		KeReleaseSpinLock(&Global.map_lock,Irql);
		if(UserData&&UserData->ispe==unknown)
		{
			if(ReadFile->ByteOffset.QuadPart==0&&Irp->IoStatus.Information>sizeof(IMAGE_DOS_HEADER))
			{
				IMAGE_DOS_HEADER *header=Irp->AssociatedIrp.SystemBuffer;
				if(header==NULL)
				{
					header=Irp->UserBuffer;
					if(header==NULL)
						header=MmGetSystemAddressForMdlSafe(Irp->MdlAddress,NormalPagePriority);
				}
				ASSERT(header);
					UserData->ispe=IsPe(header,(ULONG)Irp->IoStatus.Information);
			}
		}
	}
	KeSetEvent(&ReadFile->Kevent,IO_NO_INCREMENT,false);
	return STATUS_SUCCESS;
}

NTSTATUS DeviceRead(DEVICE_OBJECT *DeviceObject,IRP *Irp)
{
	DEVICEEXTENSION *DeviceExtension=(DEVICEEXTENSION*)DeviceObject->DeviceExtension;
	if(DeviceExtension->IsControl==false)
	{
		IO_STACK_LOCATION *stack=IoGetCurrentIrpStackLocation(Irp);
		FILE_OBJECT *FileObject=stack->FileObject;
		KIRQL Irql;
		KeAcquireSpinLock(&Global.wait_lock,&Irql);
		if(Global.pass)
		{
			InterlockedIncrement(&Global.wait_number);
			KeReleaseSpinLock(&Global.wait_lock,Irql);
			READFILE ReadFile={.FileObject=FileObject,.ByteOffset=stack->Parameters.Read.ByteOffset,.Length=stack->Parameters.Read.Length};
			KeInitializeEvent(&ReadFile.Kevent,NotificationEvent,false);
			IoCopyCurrentIrpStackLocationToNext(Irp);
			IoSetCompletionRoutine(Irp,ReadComplete,&ReadFile,true,true,true);
			IoCallDriver(DeviceExtension->Next,Irp);
			KeWaitForSingleObject(&ReadFile.Kevent,Executive,KernelMode,false,NULL);
			if(NT_SUCCESS(ReadFile.Status))
			{
				KdPrintEx(0,0,"Request Process:%d\nRequest Mode:%s\nRequest Path:%c:%wZ\n",PsGetCurrentProcessId(),"Read",
					DeviceExtension->Drive+'C',&FileObject->FileName);
				KeAcquireSpinLock(&Global.map_lock,&Irql);
				USERDATA *UserData=Global.map->Get(Global.map,(unsigned long long)FileObject/sizeof(FILE_OBJECT));
				KeReleaseSpinLock(&Global.map_lock,Irql);
				if(UserData)
				{
					if(UserData->ispe==unknown)
						ReadHeader(DeviceObject,FileObject,UserData);
					KeAcquireSpinLock(&Global.write_lock,&Irql);
						if(InsertRecord(&Global.head[Global.index],DeviceExtension->Drive,&FileObject->FileName,Read,ReadFile.ByteOffset.QuadPart,
							ReadFile.Length,UserData->ispe)&&++Global.index==QueueLength)
							Global.index=0;
					KeReleaseSpinLock(&Global.write_lock,Irql);
				}
			}
			InterlockedDecrement(&Global.wait_number);
			return ReadFile.Status;
		}
		else
			KeReleaseSpinLock(&Global.wait_lock,Irql);
	}
	return PassThrough(DeviceObject,Irp);
}

NTSTATUS WriteComplete(DEVICE_OBJECT *DeviceObject,IRP *Irp,PVOID Context)
{
	WRITEFILE *WriteFile=(WRITEFILE*)Context;
	WriteFile->Status=Irp->IoStatus.Status;
	KeSetEvent(&WriteFile->Kevent,IO_NO_INCREMENT,false);
	return STATUS_SUCCESS;
}

NTSTATUS DeviceWrite(DEVICE_OBJECT *DeviceObject,IRP *Irp)
{
	DEVICEEXTENSION *DeviceExtension=(DEVICEEXTENSION*)DeviceObject->DeviceExtension;
	if(DeviceExtension->IsControl==false)
	{
		IO_STACK_LOCATION *stack=IoGetCurrentIrpStackLocation(Irp);
		FILE_OBJECT *FileObject=stack->FileObject;
		KIRQL Irql;
		KeAcquireSpinLock(&Global.wait_lock,&Irql);
		if(Global.pass)
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
				long long offset=stack->Parameters.Write.ByteOffset.QuadPart;
				unsigned long length=stack->Parameters.Write.Length;
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
						MDL *mdl=Irp->MdlAddress;
						if(mdl==NULL)
						{
							if(Irp->AssociatedIrp.SystemBuffer)
								mdl=IoAllocateMdl(Irp->AssociatedIrp.SystemBuffer,stack->Parameters.Write.Length,false,false,false);
							else
								mdl=IoAllocateMdl(Irp->UserBuffer,stack->Parameters.Write.Length,false,false,false);
						}
						MmProbeAndLockPages(mdl,KernelMode,IoReadAccess);
						KAPC_STATE apc_state;
						KeStackAttachProcess(Global.process,&apc_state);
						query->mapped=MmMapLockedPagesSpecifyCache(mdl,UserMode,MmCached,NULL,false,NormalPagePriority);
						KeUnstackDetachProcess(&apc_state);
						query->allow=allow_unknown;
						query->waiting=false;
						InsertRecord(&query->record,DeviceExtension->Drive,&FileObject->FileName,Write,offset,length,UserData->ispe);
						while(query->allow==allow_unknown);
						KeStackAttachProcess(Global.process,&apc_state);
						MmUnmapLockedPages(query->mapped,mdl);
						KeUnstackDetachProcess(&apc_state);
						MmUnlockPages(mdl);
						IoFreeMdl(mdl);
						if(query->allow==allow_true)
						{
							WRITEFILE WriteFile;
							KeInitializeEvent(&WriteFile.Kevent,NotificationEvent,false);
							IoCopyCurrentIrpStackLocationToNext(Irp);
							IoSetCompletionRoutine(Irp,WriteComplete,&WriteFile,true,true,true);
							IoCallDriver(DeviceExtension->Next,Irp);
							KeWaitForSingleObject(&WriteFile.Kevent,Executive,KernelMode,false,NULL);
							if(NT_SUCCESS(WriteFile.Status))
							{
								KdPrintEx(0,0,"Request Process:%d\nRequest Mode:%s\nRequest Path:%c:%wZ\n",PsGetCurrentProcessId(),"Write",
									DeviceExtension->Drive+'C',&FileObject->FileName);
								query->allow=allow_success;
							}
							else
								query->allow=allow_fail;
							InterlockedDecrement(&Global.wait_number);
							return WriteFile.Status;
						}
						else
						{
							Irp->IoStatus.Status=STATUS_UNSUCCESSFUL;
							Irp->IoStatus.Information=0;
							IoCompleteRequest(Irp,IO_NO_INCREMENT);
							InterlockedDecrement(&Global.wait_number);
							return STATUS_UNSUCCESSFUL;
						}
					}
					else
						DbgPrintEx(0,0,"Insufficient memory in circular linked query list.\n");
				}
				else
				{
					WRITEFILE WriteFile;
					KeInitializeEvent(&WriteFile.Kevent,NotificationEvent,false);
					IoCopyCurrentIrpStackLocationToNext(Irp);
					IoSetCompletionRoutine(Irp,WriteComplete,&WriteFile,true,true,true);
					IoCallDriver(DeviceExtension->Next,Irp);
					KeWaitForSingleObject(&WriteFile.Kevent,Executive,KernelMode,false,NULL);
					if(NT_SUCCESS(WriteFile.Status))
					{
						KdPrintEx(0,0,"Request Process:%d\nRequest Mode:%s\nRequest Path:%c:%wZ\n",PsGetCurrentProcessId(),"Write",
							DeviceExtension->Drive+'C',&FileObject->FileName);
						KeAcquireSpinLock(&Global.write_lock,&Irql);
						if(InsertRecord(&Global.head[Global.index],DeviceExtension->Drive,&FileObject->FileName,Write,
							offset,length,UserData->ispe)&&++Global.index==QueueLength)
							Global.index=0;
						KeReleaseSpinLock(&Global.write_lock,Irql);
					}
					InterlockedDecrement(&Global.wait_number);
					return WriteFile.Status;
				}
			}
			InterlockedDecrement(&Global.wait_number);
		}
		else
			KeReleaseSpinLock(&Global.wait_lock,Irql);
	}
	return PassThrough(DeviceObject,Irp);
}

NTSTATUS MountVolumeComplete(DEVICE_OBJECT *DeviceObject,IRP *Irp,PVOID Context)
{
	IO_STACK_LOCATION *stack=IoGetCurrentIrpStackLocation(Irp);
	MOUNTVOLUME *MountVolume=(MOUNTVOLUME*)Context;
	MountVolume->Status=Irp->IoStatus.Status;
	if(stack->Parameters.MountVolume.Vpb)
	{
		MountVolume->LogicalVolume=stack->Parameters.MountVolume.Vpb->RealDevice;
		MountVolume->FileSystemDevice=stack->Parameters.MountVolume.Vpb->DeviceObject;
	}
	KeSetEvent(&MountVolume->Kevent,IO_NO_INCREMENT,false);
	return STATUS_SUCCESS;
}

NTSTATUS DeviceFileSystemControl(DEVICE_OBJECT *DeviceObject,IRP *Irp)
{
	ASSERT(DeviceObject!=Global.interactive);
	DEVICEEXTENSION *DeviceExtension=(DEVICEEXTENSION*)DeviceObject->DeviceExtension;
	IO_STACK_LOCATION *stack=IoGetCurrentIrpStackLocation(Irp);
	switch(stack->MinorFunction)
	{
		case IRP_MN_MOUNT_VOLUME:
		{
			MOUNTVOLUME MountVolume={.LogicalVolume=NULL,.FileSystemDevice=NULL};
			KeInitializeEvent(&MountVolume.Kevent,NotificationEvent,false);
			IoCopyCurrentIrpStackLocationToNext(Irp);
			IoSetCompletionRoutine(Irp,MountVolumeComplete,&MountVolume,true,true,true);
			IoCallDriver(DeviceExtension->Next,Irp);
			KeWaitForSingleObject(&MountVolume.Kevent,Executive,KernelMode,false,NULL);
			if(NT_SUCCESS(MountVolume.Status)&&MountVolume.FileSystemDevice&&MountVolume.LogicalVolume)
			{
				DeviceObject=Attach(DeviceObject->DriverObject,MountVolume.FileSystemDevice);
				if(DeviceObject)
				{
					UNICODE_STRING DosName;
					IoVolumeDeviceToDosName(MountVolume.LogicalVolume,&DosName);
					DeviceExtension=(DEVICEEXTENSION*)DeviceObject->DeviceExtension;
					DeviceExtension->Drive=DosName.Buffer[0]-'C';
					Global.fs_count++;
				}
			}
			return MountVolume.Status;
		}
		break;
	}
	IoSkipCurrentIrpStackLocation(Irp);
	return IoCallDriver(DeviceExtension->Next,Irp);
}

NTSTATUS DeviceControl(DEVICE_OBJECT *DeviceObject,IRP *Irp)
{
	if(DeviceObject!=Global.interactive)
	{
		DEVICEEXTENSION *DeviceExtension=(DEVICEEXTENSION*)DeviceObject->DeviceExtension;
		IoSkipCurrentIrpStackLocation(Irp);
		return IoCallDriver(DeviceExtension->Next,Irp);
	}
	NTSTATUS status=STATUS_UNSUCCESSFUL;
	ULONG_PTR size=0;
	IO_STACK_LOCATION *stack=IoGetCurrentIrpStackLocation(Irp);
	ULONG IoControlCode=stack->Parameters.DeviceIoControl.IoControlCode;
	ULONG InputLength=stack->Parameters.DeviceIoControl.InputBufferLength;
	ULONG OutputLength=stack->Parameters.DeviceIoControl.OutputBufferLength;
	switch(IoControlCode)
	{
		case STARTMONITOR:
			Global.mdl=IoAllocateMdl(Irp->UserBuffer,OutputLength,false,false,NULL);
			Global.mdl_query=IoAllocateMdl(stack->Parameters.DeviceIoControl.Type3InputBuffer,InputLength,false,false,NULL);
			if(Global.mdl&&Global.mdl_query&&NT_SUCCESS(PsLookupProcessByProcessId(PsGetCurrentProcessId(),&Global.process)))
			{
				MmProbeAndLockPages(Global.mdl,KernelMode,IoWriteAccess);
				MmProbeAndLockPages(Global.mdl_query,KernelMode,IoWriteAccess);
				Global.head=MmMapLockedPagesSpecifyCache(Global.mdl,KernelMode,MmCached,NULL,false,NormalPagePriority);
				Global.query=MmMapLockedPagesSpecifyCache(Global.mdl_query,KernelMode,MmCached,NULL,false,NormalPagePriority);
				if(Global.head&&Global.query)
				{
					KeInitializeSpinLock(&Global.map_lock);
					Global.map=unordered_map_Constructor(128);
					KeInitializeSpinLock(&Global.write_lock);
					ExInitializeNPagedLookasideList(&Global.lookaside,NULL,NULL,0,header_size,0,0);
					KeInitializeSpinLock(&Global.query_lock);
					KeInitializeSpinLock(&Global.wait_lock);
					Global.pass=true;
					status=STATUS_SUCCESS;
					Irp->IoStatus.Information=size;
					Irp->IoStatus.Status=status;
					IoCompleteRequest(Irp,IO_NO_INCREMENT);
					return status;
				}
				if(Global.head)
					MmUnmapLockedPages(Global.head,Global.mdl);
				if(Global.query)
					MmUnmapLockedPages(Global.query,Global.mdl_query);
				MmUnlockPages(Global.mdl);
				MmUnlockPages(Global.mdl_query);
			}
			if(Global.mdl)
				IoFreeMdl(Global.mdl);
			if(Global.mdl_query)
				IoFreeMdl(Global.mdl_query);
			break;

		case STOPMONITOR:
			if(Global.pass)
			{
				KIRQL Irql;
				KeAcquireSpinLock(&Global.wait_lock,&Irql);
				Global.pass=false;
				while(Global.wait_number)
				{
					KeReleaseSpinLock(&Global.wait_lock,Irql);
					KeAcquireSpinLock(&Global.wait_lock,&Irql);
				}
				KeReleaseSpinLock(&Global.wait_lock,Irql);
				ExDeleteNPagedLookasideList(&Global.lookaside);
				Global.map->Destructor(Global.map);
				Global.map=NULL;
				MmUnmapLockedPages(Global.head,Global.mdl);
				MmUnlockPages(Global.mdl);
				IoFreeMdl(Global.mdl);
				Global.head=NULL;
				Global.mdl=NULL;
				MmUnmapLockedPages(Global.query,Global.mdl_query);
				MmUnlockPages(Global.mdl_query);
				IoFreeMdl(Global.mdl_query);
				Global.query=NULL;
				Global.mdl_query=NULL;
			}
			status=STATUS_SUCCESS;
			break;
	}
	Irp->IoStatus.Information=size;
	Irp->IoStatus.Status=status;
	IoCompleteRequest(Irp,IO_NO_INCREMENT);
	return status;
}

NTSTATUS DeviceClose(DEVICE_OBJECT *DeviceObject,IRP *Irp)
{
	DEVICEEXTENSION *DeviceExtension=(DEVICEEXTENSION*)DeviceObject->DeviceExtension;
	KIRQL Irql;
	KeAcquireSpinLock(&Global.wait_lock,&Irql);
	if(Global.pass&&DeviceExtension->IsControl==false)
	{
		InterlockedIncrement(&Global.wait_number);
		KeReleaseSpinLock(&Global.wait_lock,Irql);
		FILE_OBJECT *FileObject=IoGetCurrentIrpStackLocation(Irp)->FileObject;
		KeAcquireSpinLock(&Global.map_lock,&Irql);
		Global.map->Erase(Global.map,Global.map->Get(Global.map,(unsigned long long)FileObject/sizeof(FILE_OBJECT)));
		KeReleaseSpinLock(&Global.map_lock,Irql);
		InterlockedDecrement(&Global.wait_number);
	}
	else
		KeReleaseSpinLock(&Global.wait_lock,Irql);
	return PassThrough(DeviceObject,Irp);
}

NTSTATUS SurpriseRemovalComplete(DEVICE_OBJECT *DeviceObject,IRP *Irp,PVOID Context)
{
	SURPRISEREMOVAL *SurpriseRemoval=(SURPRISEREMOVAL*)Context;
	SurpriseRemoval->Status=Irp->IoStatus.Status;
	KeSetEvent(&SurpriseRemoval->Kevent,IO_NO_INCREMENT,false);
	return STATUS_SUCCESS;
}

NTSTATUS DevicePnp(DEVICE_OBJECT *DeviceObject,IRP *Irp)
{
	ASSERT(DeviceObject!=Global.interactive);
	DEVICEEXTENSION *DeviceExtension=(DEVICEEXTENSION*)DeviceObject->DeviceExtension;
	IO_STACK_LOCATION *stack=IoGetCurrentIrpStackLocation(Irp);
	switch(stack->MinorFunction)
	{
		case IRP_MN_SURPRISE_REMOVAL:
		{
			SURPRISEREMOVAL SurpriseRemoval;
			KeInitializeEvent(&SurpriseRemoval.Kevent,NotificationEvent,false);
			IoCopyCurrentIrpStackLocationToNext(Irp);
			IoSetCompletionRoutine(Irp,SurpriseRemovalComplete,&SurpriseRemoval,true,true,true);
			IoCallDriver(DeviceExtension->Next,Irp);
			KeWaitForSingleObject(&SurpriseRemoval.Kevent,Executive,KernelMode,false,NULL);
			if(NT_SUCCESS(SurpriseRemoval.Status))
			{
				IoDetachDevice(DeviceExtension->Next);
				IoDeleteDevice(DeviceObject);
			}
			return SurpriseRemoval.Status;
		}
		break;
	}
	IoSkipCurrentIrpStackLocation(Irp);
	return IoCallDriver(DeviceExtension->Next,Irp);
}

void DriverUnload(DRIVER_OBJECT *DriverObject)
{
	IoUnregisterFsRegistrationChange(DriverObject,FileSystemChange);
	UNICODE_STRING SymbolicLinkName;
	RtlInitUnicodeString(&SymbolicLinkName,L"\\??\\Monitor");
	IoDeleteSymbolicLink(&SymbolicLinkName);
	DEVICE_OBJECT *DeviceObject=DriverObject->DeviceObject,*NextDevice=NULL;
	while(DeviceObject)
	{
		DEVICEEXTENSION *DeviceExtension=(DEVICEEXTENSION*)DeviceObject->DeviceExtension;
		if(DeviceExtension&&DeviceExtension->Next)
			IoDetachDevice(DeviceExtension->Next);
		NextDevice=DeviceObject->NextDevice;
		IoDeleteDevice(DeviceObject);
		DeviceObject=NextDevice;
	}
	if(DriverObject->FastIoDispatch)
	{
		ExFreePool(DriverObject->FastIoDispatch);
		DriverObject->FastIoDispatch=NULL;
	}
}

#pragma code_seg("INIT")
NTSTATUS DriverEntry(DRIVER_OBJECT *DriverObject,UNICODE_STRING *RegistryPath)
{
	for(int i=0;i<IRP_MJ_MAXIMUM_FUNCTION;i++)
		DriverObject->MajorFunction[i]=PassThrough;
	DriverObject->MajorFunction[IRP_MJ_CREATE]=DeviceCreate;
	DriverObject->MajorFunction[IRP_MJ_READ]=DeviceRead;
	DriverObject->MajorFunction[IRP_MJ_WRITE]=DeviceWrite;
	DriverObject->MajorFunction[IRP_MJ_FILE_SYSTEM_CONTROL]=DeviceFileSystemControl;
	DriverObject->MajorFunction[IRP_MJ_DEVICE_CONTROL]=DeviceControl;
	DriverObject->MajorFunction[IRP_MJ_CLOSE]=DeviceClose;
	DriverObject->MajorFunction[IRP_MJ_PNP]=DevicePnp;
	DriverObject->DriverUnload=DriverUnload;

	NTSTATUS status=STATUS_NO_MEMORY;
	DriverObject->FastIoDispatch=(FAST_IO_DISPATCH*)ExAllocatePool(PagedPool,sizeof(FAST_IO_DISPATCH));
	if(DriverObject->FastIoDispatch)
	{
		InitializeFastIoDispatch(DriverObject->FastIoDispatch);
		UNICODE_STRING DeviceName;
		RtlInitUnicodeString(&DeviceName,L"\\Device\\FilterDriverDevice");
		DEVICE_OBJECT *DeviceObject;
		status=IoCreateDevice(DriverObject,sizeof(DEVICEEXTENSION),&DeviceName,FILE_DEVICE_UNKNOWN,0,FALSE,&DeviceObject);
		if(NT_SUCCESS(status))
		{
			DEVICEEXTENSION *DeviceExtension=(DEVICEEXTENSION*)DeviceObject->DeviceExtension;
			DeviceExtension->IsControl=true;
		#ifdef DBG
			DeviceExtension->Global=&Global;
		#endif // DBG
			Global.interactive=DeviceObject;
			UNICODE_STRING SymbolicLinkName;
			RtlInitUnicodeString(&SymbolicLinkName,L"\\??\\Monitor");
			status=IoCreateSymbolicLink(&SymbolicLinkName,&DeviceName);
			if(NT_SUCCESS(status))
			{
				status=IoRegisterFsRegistrationChange(DriverObject,FileSystemChange);
				if(NT_SUCCESS(status))
				{
					AttachToVolume(DriverObject);
					KdPrintEx(0,0,"%d\n",Global.fs_count);
					if(Global.fs_count!=0)
						return STATUS_SUCCESS;
					else
						status=STATUS_UNSUCCESSFUL;
				}
			}
		}
	}
	return status;
}