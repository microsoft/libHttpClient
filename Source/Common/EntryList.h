// Copyright (c) Microsoft Corporation
// Licensed under the MIT license. See LICENSE file in the project root for full license information.
#pragma once
__forceinline void InitializeListHead(PLIST_ENTRY entry)
{
    entry->Blink = entry->Flink = entry;
}

__forceinline void InsertHeadList(PLIST_ENTRY head, PLIST_ENTRY node)
{
    head->Flink->Blink = node;
    node->Flink = head->Flink;
    head->Flink = node;
    node->Blink = head;
}

__forceinline void InsertTailList(PLIST_ENTRY head, PLIST_ENTRY node)
{
    head->Blink->Flink = node;
    node->Blink = head->Blink;
    head->Blink = node;
    node->Flink = head;
}

__forceinline PLIST_ENTRY RemoveHeadList(PLIST_ENTRY head)
{
    PLIST_ENTRY node = head->Flink;
    head->Flink = node->Flink;
    head->Flink->Blink = head;
    return node;
}

__forceinline BOOLEAN RemoveEntryList(PLIST_ENTRY entry)
{
    entry->Blink->Flink = entry->Flink;
    entry->Flink->Blink = entry->Blink;
    return entry->Blink == entry->Blink->Flink;
}
