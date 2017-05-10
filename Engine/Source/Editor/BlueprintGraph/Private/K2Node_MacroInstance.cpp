// Copyright 1998-2017 Epic Games, Inc. All Rights Reserved.

#include "K2Node_MacroInstance.h"
#include "Engine/Blueprint.h"
#include "Framework/Commands/UIAction.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "EdGraphSchema_K2.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "EditorStyleSet.h"
#include "Editor.h"
#include "EditorCategoryUtils.h"
#include "BlueprintActionFilter.h"

#define LOCTEXT_NAMESPACE "K2Node_MacroInstance"

UK2Node_MacroInstance::UK2Node_MacroInstance(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	bReconstructNode = false;
}

void UK2Node_MacroInstance::Serialize(FArchive& Ar)
{
	Super::Serialize(Ar);

	if (Ar.UE4Ver() < VER_UE4_K2NODE_REFERENCEGUIDS)
	{
		MacroGraphReference.SetGraph(MacroGraph_DEPRECATED);
	}
}

bool UK2Node_MacroInstance::IsActionFilteredOut(FBlueprintActionFilter const& Filter)
{
	bool bIsFilteredOut = false;
	FBlueprintActionContext const& FilterContext = Filter.Context;

	for (UEdGraph* Graph : FilterContext.Graphs)
	{
		if (!CanPasteHere(Graph))
		{
			bIsFilteredOut = true;
			break;
		}
	}
	return bIsFilteredOut;
}

void UK2Node_MacroInstance::PostPasteNode()
{
	const UBlueprint* InstanceOwner = GetBlueprint();

	// Find the owner of the macro graph
	const UEdGraph* MacroGraph = MacroGraphReference.GetGraph();
	UObject* MacroOwner = MacroGraph->GetOuter();
	UBlueprint* MacroOwnerBP = NULL;
	while(MacroOwner)
	{
		MacroOwnerBP = Cast<UBlueprint>(MacroOwner);
		if(MacroOwnerBP)
		{
			break;
		}

		MacroOwner = MacroOwner->GetOuter();
	}
	
	if((MacroOwnerBP != NULL)
		&& (MacroOwnerBP->BlueprintType != BPTYPE_MacroLibrary)
		&& (MacroOwnerBP != InstanceOwner))
	{
		// If this is a graph from another blueprint that is NOT a library, disallow the connection!
		MacroGraphReference.SetGraph(NULL);
	}

	Super::PostPasteNode();
}

void UK2Node_MacroInstance::AllocateDefaultPins()
{
	UK2Node::AllocateDefaultPins();

	const UEdGraphSchema_K2* Schema = GetDefault<UEdGraphSchema_K2>();
	PreloadObject(MacroGraphReference.GetBlueprint());
	UEdGraph* MacroGraph = MacroGraphReference.GetGraph();
	
	if (MacroGraph != NULL)
	{
		PreloadObject(MacroGraph);

		// Preload the macro graph, if needed, so that we can get the proper pins
		if (MacroGraph->HasAnyFlags(RF_NeedLoad))
		{
			PreloadObject(MacroGraph);
			FBlueprintEditorUtils::PreloadMembers(MacroGraph);
		}

		for (TArray<UEdGraphNode*>::TIterator NodeIt(MacroGraph->Nodes); NodeIt; ++NodeIt)
		{
			if (UK2Node_Tunnel* TunnelNode = Cast<UK2Node_Tunnel>(*NodeIt))
			{
				// Only want exact tunnel nodes, more specific nodes like composites or macro instances shouldn't be grabbed.
				if (TunnelNode->GetClass() == UK2Node_Tunnel::StaticClass())
				{
					for (TArray<UEdGraphPin*>::TIterator PinIt(TunnelNode->Pins); PinIt; ++PinIt)
					{
						UEdGraphPin* PortPin = *PinIt;

						// We're not interested in any pins that have been expanded internally on the macro
						if (PortPin->ParentPin == NULL)
						{
							UEdGraphPin* NewLocalPin = CreatePin(UEdGraphPin::GetComplementaryDirection(PortPin->Direction), PortPin->PinType, PortPin->PinName);
							Schema->SetPinAutogeneratedDefaultValue(NewLocalPin, PortPin->GetDefaultAsString());
						}
					}
				}
			}
		}
	}
}

FText UK2Node_MacroInstance::GetTooltipText() const
{
	UEdGraph* MacroGraph = MacroGraphReference.GetGraph();
	if (FKismetUserDeclaredFunctionMetadata* Metadata = GetAssociatedGraphMetadata(MacroGraph))
	{
		if (!Metadata->ToolTip.IsEmpty())
		{
			return Metadata->ToolTip;
		}
	}

	if (MacroGraph == nullptr)
	{
		return NSLOCTEXT("K2Node", "Macro_Tooltip", "Macro");
	}
	else if (CachedTooltip.IsOutOfDate(this))
	{
		// FText::Format() is slow, so we cache this to save on performance
		CachedTooltip.SetCachedText(FText::Format(NSLOCTEXT("K2Node", "MacroGraphInstance_Tooltip", "{0} instance"), FText::FromName(MacroGraph->GetFName())), this);
	}
	return CachedTooltip;
}

FText UK2Node_MacroInstance::GetKeywords() const
{
	FText Keywords = GetAssociatedGraphMetadata(GetMacroGraph())->Keywords;

	// If the Macro has Compact Node Title data, append the compact node title as a Keyword so it can be searched.
	if (ShouldDrawCompact())
	{
		Keywords = FText::Format(FText::FromString(TEXT("{0} {1}")), Keywords, GetCompactNodeTitle());
	}
	return Keywords;
}

FText UK2Node_MacroInstance::GetNodeTitle(ENodeTitleType::Type TitleType) const
{
	UEdGraph* MacroGraph = MacroGraphReference.GetGraph();
	FText Result = NSLOCTEXT("K2Node", "MacroInstance", "Macro instance");
	if (MacroGraph)
	{
		Result = FText::FromString(MacroGraph->GetName());
	}

	return Result;
}

FLinearColor UK2Node_MacroInstance::GetNodeTitleColor() const
{
	UEdGraph* MacroGraph = MacroGraphReference.GetGraph();
	if (FKismetUserDeclaredFunctionMetadata* Metadata = GetAssociatedGraphMetadata(MacroGraph))
	{
		return Metadata->InstanceTitleColor.ToFColor(false);
	}

	return FLinearColor::White;
}

void UK2Node_MacroInstance::GetContextMenuActions(const FGraphNodeContextMenuBuilder& Context) const
{
	if ( Context.Pin == NULL )
	{
		Context.MenuBuilder->BeginSection("K2NodeMacroInstance", NSLOCTEXT("K2Node", "MacroInstanceHeader", "Macro Instance"));
		{
			Context.MenuBuilder->AddMenuEntry(
				NSLOCTEXT("K2Node", "MacroInstanceFindInContentBrowser", "Find in Content Browser"),
				NSLOCTEXT("K2Node", "MacroInstanceFindInContentBrowserTooltip", "Finds the Blueprint Macro Library that contains this Macro in the Content Browser"),
				FSlateIcon(FEditorStyle::GetStyleSetName(), "PropertyWindow.Button_Browse"),
				FUIAction( FExecuteAction::CreateStatic( &UK2Node_MacroInstance::FindInContentBrowser, TWeakObjectPtr<UK2Node_MacroInstance>(this) ) )
				);
		}
		Context.MenuBuilder->EndSection();
	}
}

FKismetUserDeclaredFunctionMetadata* UK2Node_MacroInstance::GetAssociatedGraphMetadata(const UEdGraph* AssociatedMacroGraph)
{
	if (AssociatedMacroGraph)
	{
		// Look at the graph for the entry node, to get the default title color
		TArray<UK2Node_Tunnel*> TunnelNodes;
		AssociatedMacroGraph->GetNodesOfClass(TunnelNodes);

		for (int32 i = 0; i < TunnelNodes.Num(); i++)
		{
			UK2Node_Tunnel* Node = TunnelNodes[i];
			if (Node->IsEditable())
			{
				if (Node->bCanHaveOutputs)
				{
					return &(Node->MetaData);
				}
			}
		}
	}

	return NULL;
}

void UK2Node_MacroInstance::FindInContentBrowser(TWeakObjectPtr<UK2Node_MacroInstance> MacroInstance)
{
	if ( MacroInstance.IsValid() )
	{
		UEdGraph* InstanceMacroGraph = MacroInstance.Get()->MacroGraphReference.GetGraph();
		if ( InstanceMacroGraph )
		{
			UBlueprint* BlueprintToSync = FBlueprintEditorUtils::FindBlueprintForGraph(InstanceMacroGraph);
			if ( BlueprintToSync )
			{
				TArray<UObject*> ObjectsToSync;
				ObjectsToSync.Add( BlueprintToSync );
				GEditor->SyncBrowserToObjects(ObjectsToSync);
			}
		}
	}
}




void UK2Node_MacroInstance::NotifyPinConnectionListChanged(UEdGraphPin* ChangedPin)
{
	Super::NotifyPinConnectionListChanged(ChangedPin);

	const UEdGraphSchema_K2* const Schema = GetDefault<UEdGraphSchema_K2>();

	// added a link?
	if (ChangedPin->LinkedTo.Num() > 0)
	{
		// ... to a wildcard pin?
		bool const bIsWildcardPin = ChangedPin->PinType.PinCategory == Schema->PC_Wildcard;
		if (bIsWildcardPin)
		{
			// get type of pin we just got linked to
			FEdGraphPinType const LinkedPinType = ChangedPin->LinkedTo[0]->PinType;

			// change all other wildcard pins to the new type
			// note we're assuming only one wildcard type per Macro node, for now

			for(int32 PinIdx=0; PinIdx<Pins.Num(); PinIdx++)
			{
				UEdGraphPin* const TmpPin = Pins[PinIdx];
				if (TmpPin)
				{
					if (TmpPin->PinType.PinCategory == Schema->PC_Wildcard)
					{
						// only copy the category stuff to preserve array and ref status
						TmpPin->PinType.PinCategory = LinkedPinType.PinCategory;
						TmpPin->PinType.PinSubCategory = LinkedPinType.PinSubCategory;
						TmpPin->PinType.PinSubCategoryObject = LinkedPinType.PinSubCategoryObject;
					}
				}
			}

			ResolvedWildcardType = LinkedPinType;
			bReconstructNode = true;
		}
	}
 	else
	{
		// reconstruct on disconnects so we can revert back to wildcards if necessary
		bReconstructNode = true;
	}
}



void UK2Node_MacroInstance::NodeConnectionListChanged()
{
	Super::NodeConnectionListChanged();

	if (bReconstructNode)
	{
		ReconstructNode();

		UBlueprint* const Blueprint = GetBlueprint();
		if (Blueprint && !Blueprint->bBeingCompiled)
		{
			FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);
			Blueprint->BroadcastChanged();
		}
	}
}

FString UK2Node_MacroInstance::GetDocumentationLink() const
{
	return TEXT("Shared/GraphNodes/Blueprint/UK2Node_MacroInstance");
}

FString UK2Node_MacroInstance::GetDocumentationExcerptName() const
{
	UEdGraph* MacroGraph = MacroGraphReference.GetGraph();
	if (MacroGraph != NULL)
	{
		return MacroGraph->GetName();
	}
	return Super::GetDocumentationExcerptName();
}

void UK2Node_MacroInstance::PostReconstructNode()
{
	bReconstructNode = false;

	Super::PostReconstructNode();
}

FName UK2Node_MacroInstance::GetCornerIcon() const
{
	if (UEdGraph* MacroGraph = MacroGraphReference.GetGraph())
	{
		FBlueprintMacroCosmeticInfo CosmeticInfo = FBlueprintEditorUtils::GetCosmeticInfoForMacro(MacroGraph);
		if (CosmeticInfo.bContainsLatentNodes)
		{
			return TEXT("Graph.Latent.LatentIcon");
		}
	}

	return Super::GetCornerIcon();
}

FSlateIcon UK2Node_MacroInstance::GetIconAndTint(FLinearColor& OutColor) const
{
	const char* IconName = "GraphEditor.Macro_16x";

	// Special case handling for standard macros
	// @TODO Change this to a SlateBurushAsset pointer on the graph or something similar, to allow any macro to have an icon
	UEdGraph* MacroGraph = MacroGraphReference.GetGraph();
	if(MacroGraph != NULL && MacroGraph->GetOuter()->GetName() == TEXT("StandardMacros"))
	{
		FName MacroName = FName(*MacroGraph->GetName());
		if(	MacroName == TEXT("ForLoop" ) ||
			MacroName == TEXT("ForLoopWithBreak") ||
			MacroName == TEXT("WhileLoop") )
		{
			IconName = "GraphEditor.Macro.Loop_16x";
		}
		else if( MacroName == TEXT("Gate") )
		{
			IconName = "GraphEditor.Macro.Gate_16x";
		}
		else if( MacroName == TEXT("Do N") )
		{
			IconName = "GraphEditor.Macro.DoN_16x";
		}
		else if (MacroName == TEXT("DoOnce"))
		{
			IconName = "GraphEditor.Macro.DoOnce_16x";
		}
		else if (MacroName == TEXT("IsValid"))
		{
			IconName = "GraphEditor.Macro.IsValid_16x";
		}
		else if (MacroName == TEXT("FlipFlop"))
		{
			IconName = "GraphEditor.Macro.FlipFlop_16x";
		}
		else if ( MacroName == TEXT("ForEachLoop") ||
				  MacroName == TEXT("ForEachLoopWithBreak") )
		{
			IconName = "GraphEditor.Macro.ForEach_16x";
		}
	}

	return FSlateIcon("EditorStyle", IconName);
}

FText UK2Node_MacroInstance::GetCompactNodeTitle() const
{
	FText ResultText;
	if (FKismetUserDeclaredFunctionMetadata* MacroGraphMetadata = GetAssociatedGraphMetadata(GetMacroGraph()))
	{
		ResultText = MacroGraphMetadata->CompactNodeTitle;
	}
	return ResultText;	
}

bool UK2Node_MacroInstance::ShouldDrawCompact() const
{
	return !GetCompactNodeTitle().IsEmpty();
}

bool UK2Node_MacroInstance::CanPasteHere(const UEdGraph* TargetGraph) const
{
	bool bCanPaste = false;

	UBlueprint* MacroBlueprint  = GetSourceBlueprint();
	UBlueprint* TargetBlueprint = FBlueprintEditorUtils::FindBlueprintForGraph(TargetGraph);

	if ((MacroBlueprint != nullptr) && (TargetBlueprint != nullptr))
	{
		// Only allow "local" macro instances or instances from a macro library blueprint with the same parent class
		check(MacroBlueprint->ParentClass != nullptr && TargetBlueprint->ParentClass != nullptr);
		bCanPaste = (MacroBlueprint == TargetBlueprint) || (MacroBlueprint->BlueprintType == BPTYPE_MacroLibrary && TargetBlueprint->ParentClass->IsChildOf(MacroBlueprint->ParentClass));
	}

	// Macro Instances are not allowed in it's own graph
	UEdGraph* MacroGraph = GetMacroGraph();
	bCanPaste &= (MacroGraph != TargetGraph);
	// nor in Function graphs if the macro has latent functions in it
	bool const bIsTargetFuncGraph = (TargetGraph->GetSchema()->GetGraphType(TargetGraph) == GT_Function);
	bCanPaste &= (!bIsTargetFuncGraph || !FBlueprintEditorUtils::CheckIfGraphHasLatentFunctions(MacroGraph));

	return bCanPaste && Super::CanPasteHere(TargetGraph);
}

void UK2Node_MacroInstance::PostFixupAllWildcardPins(bool bInAllWildcardPinsUnlinked)
{
	if (bInAllWildcardPinsUnlinked)
	{
		// Reset the type to a wildcard because there are no longer any wildcard pins linked to determine a type with
		ResolvedWildcardType.ResetToDefaults();
	}
}

FText UK2Node_MacroInstance::GetActiveBreakpointToolTipText() const
{
	return LOCTEXT("ActiveBreakpointToolTip", "Execution will break inside the macro.");
}

bool UK2Node_MacroInstance::HasExternalDependencies(TArray<class UStruct*>* OptionalOutput) const
{
	UBlueprint* OtherBlueprint = MacroGraphReference.GetBlueprint();
	const bool bResult = OtherBlueprint && (OtherBlueprint != GetBlueprint());
	if (bResult && OptionalOutput)
	{
		if (UClass* OtherClass = *OtherBlueprint->GeneratedClass)
		{
			OptionalOutput->AddUnique(OtherClass);
		}

		for (UEdGraphPin* Pin : Pins)
		{
			if (Pin->PinType.PinSubCategoryObject.IsValid())
			{
				if (UStruct* Struct = Cast<UStruct>(Pin->PinType.PinSubCategoryObject.Get()))
				{
					OptionalOutput->AddUnique(Struct);
				}
				else
				{
					OptionalOutput->AddUnique(Pin->PinType.PinSubCategoryObject.Get()->GetClass());
				}
			}
		}
	}
	const bool bSuperResult = Super::HasExternalDependencies(OptionalOutput);
	return bSuperResult || bResult;
}

void UK2Node_MacroInstance::GetNodeAttributes( TArray<TKeyValuePair<FString, FString>>& OutNodeAttributes ) const
{
	FString MacroName( TEXT( "InvalidMacro" ));

	if( UEdGraph* MacroGraph = GetMacroGraph() )
	{
		MacroName = MacroGraph->GetName();
	}

	OutNodeAttributes.Add( TKeyValuePair<FString, FString>( TEXT( "Type" ), TEXT( "Macro" ) ));
	OutNodeAttributes.Add( TKeyValuePair<FString, FString>( TEXT( "Class" ), GetClass()->GetName() ));
	OutNodeAttributes.Add( TKeyValuePair<FString, FString>( TEXT( "Name" ), MacroName ));
}

FText UK2Node_MacroInstance::GetMenuCategory() const
{
	FText MenuCategory = FEditorCategoryUtils::GetCommonCategory(FCommonEditorCategory::Utilities);
	if (UEdGraph* MacroGraph = GetMacroGraph())
	{
		FKismetUserDeclaredFunctionMetadata* MacroGraphMetadata = UK2Node_MacroInstance::GetAssociatedGraphMetadata(MacroGraph);
		if ((MacroGraphMetadata != nullptr) && !MacroGraphMetadata->Category.IsEmpty())
		{
			MenuCategory = MacroGraphMetadata->Category;
		}
	}

	return MenuCategory;
}

FBlueprintNodeSignature UK2Node_MacroInstance::GetSignature() const
{
	FBlueprintNodeSignature NodeSignature = Super::GetSignature();
	NodeSignature.AddSubObject(GetMacroGraph());

	return NodeSignature;
}

#undef LOCTEXT_NAMESPACE
