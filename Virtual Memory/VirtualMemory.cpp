#include "PhysicalMemory.h"
#include "VirtualMemory.h"

/**
* Function gets a frame and clear  all its memory in the physical memory
*@param currentFrame  the frame we want to clear
*/
void clearTable(uint64_t currentFrame) {
    
    for (uint64_t i = 0; i < PAGE_SIZE; i++) {
        PMwrite(currentFrame * PAGE_SIZE + i, 0);
    }
}
/**
* Helper Function handle the case the frame is empty in the treeTraverse function
*@param virtualAddress  a frame that represent the limit of the traverse
*@param currParent  the parent of the current frame
*@param currFrame  the frame we check now
*@param isEmpty  represents if found an empty frame in the traverse
*@param emptyFrame  the empty frame (if found)
*@param emptyAddressParent  the parent frame of the empty frame
*/
void emptyCase(uint64_t virtualAddress, uint64_t currParent, uint64_t currFrame, bool *isEmpty, uint64_t *emptyFrame,
               uint64_t *emptyAddressParent) {
    if (virtualAddress != currFrame) {
        *emptyFrame = currFrame;
        *emptyAddressParent = currParent;
        *isEmpty = true;
    }
}
/**
* Helper Function that calculate the Cyclic Distance in the treeTraverse function
*@param pageSwappedIn  the page we swap in the case no empty memory available
*@param currAddress  the page we check the distance with
*@return the distance between the pages
*/
uint64_t getCyclicDistance(uint64_t pageSwappedIn, uint64_t currAddress) {
    uint64_t abs;
    if (pageSwappedIn > currAddress) {
        abs = pageSwappedIn - currAddress;
    }
    else {
        abs = currAddress - pageSwappedIn;
    }
    uint64_t cyclicDistance = 0;
    if ((NUM_PAGES - abs) < abs) {
        cyclicDistance = NUM_PAGES - abs;
    }
    else {
        cyclicDistance = abs;
    }
    return cyclicDistance;
}
/**
* Helper Function that check if the distance with current page is more than the maximal Cyclic Distance found in the treeTraverse function
*@param currParent  the parent of current checked frame
*@param currFrame  the frame we check in this iteration
*@param pageSwappedIn  the page we swap in the case no empty memory available
*@param currAddress  the page we check if hes best page to swap out so far
*@param maxCyclicDistance  the maximum distance so far
*@param cyclicParent  the parent of the best frame for the swap
*@param cyclicFrame  the best frame for the swap
*@param cyclicPage  best page for the swap
*/
void cyclicCase(uint64_t currParent, uint64_t currFrame, uint64_t pageSwappedIn, uint64_t currAddress,
                uint64_t *maxCyclicDistance, uint64_t *cyclicParent, uint64_t *cyclicFrame, uint64_t *cyclicPage) {
    uint64_t cyclicDistance = getCyclicDistance(pageSwappedIn, currAddress);
    if (cyclicDistance > *maxCyclicDistance) {
        *maxCyclicDistance = cyclicDistance;
        *cyclicParent = currParent;
        *cyclicFrame = currFrame;
        *cyclicPage = currAddress;
    }
}
/**
* Recursive Function the check for a place for the memory, choose with 3 cases:
* case 1: found empty frame else case 2
* case 2: found unused frame else case 3
* case 3: swap the page with maximum Cyclic distance.
*@param virtualAddress  the boundry of our search
*@param currAddress  the current address we're at
*@param currFrame  the current frame wee check
*@param currParent  the current frame's parent
*@param currLevel  our current level in the memory tree
*@param pageSwappedIn  the page we want to swap in
*@param isEmpty  boolean if found an empty frame
*@param emptyFrameAddress  address of empty frame(if found)
*@param emptyFrameParent  address of empty frame's parent
*@param maxFrameIndex  the maximum index in the frame
*@param maxCyclicDistance  the maximum cyclic distance so far
*@param cyclicFrame  the frame of the maximum cyclic distance page so far
*@param cyclicPage  the page of the maximum cyclic distance page so far
*@param cyclicParent  the parent of the frame of the maximum cyclic distance page so far
*/
void treeTraverse(uint64_t virtualAddress, uint64_t currAddress, uint64_t currFrame, uint64_t currParent,
                  uint64_t currLevel, uint64_t pageSwappedIn,
                  bool* isEmpty, uint64_t* emptyFrameAddress, uint64_t* emptyFrameParent,
                  uint64_t* maxFrameIndex,
                  uint64_t* maxCyclicDistance, uint64_t* cyclicFrame, uint64_t* cyclicPage, uint64_t* cyclicParent) {
    if (*isEmpty) {
        return;
    }
    if (currLevel == TABLES_DEPTH) {
        cyclicCase(currParent, currFrame, pageSwappedIn, currAddress,
                   maxCyclicDistance, cyclicParent, cyclicFrame, cyclicPage);
        return;
    }
    word_t value;
    bool haveKids = false;
    for (uint64_t i = 0; i < PAGE_SIZE; i++) {
        PMread(currFrame * PAGE_SIZE + i, &value);
        if (value != 0) {
            haveKids = true;
            if ((uint64_t) value > *maxFrameIndex) {
                *maxFrameIndex = value;
            }
            uint64_t newAddress = (currAddress << OFFSET_WIDTH) + i;
            uint64_t newParent = currFrame * PAGE_SIZE + i;
            treeTraverse(virtualAddress, newAddress, value,
                         newParent, currLevel + 1,
                         pageSwappedIn, isEmpty, emptyFrameAddress, emptyFrameParent,
                         maxFrameIndex,
                         maxCyclicDistance, cyclicFrame, cyclicPage, cyclicParent);
            if (*isEmpty)
                return;
        }
    }
    if (!haveKids) {
        emptyCase(virtualAddress, currParent, currFrame,
                  isEmpty, emptyFrameAddress, emptyFrameParent);
    }
}
/**
* get the frame address with the case found in the traverse
*@param isEmpty  boolean if found an empty frame
*@param emptyFrameAddress  address of empty frame(if found)
*@param emptyFrameParent  address of empty frame's parent
*@param maxFrameIndex  the maximum index in the frame
*@param cyclicFrame  the frame of the maximum cyclic distance page
*@param cyclicPage  the page of the maximum cyclic distance page
*@param cyclicParent  the parent of the frame of the maximum cyclic distance page
*/
uint64_t getFrameAddressByCases(bool isEmpty, uint64_t emptyFrameAddress, uint64_t emptyFrameParent,
                                uint64_t maxFrameIndex, uint64_t cyclicParent, uint64_t cyclicFrame,
                                uint64_t cyclicPage) {
    //case 1: A frame containing an empty table
    if (isEmpty) {
        PMwrite(emptyFrameParent, 0);
        return emptyFrameAddress;
    }

    //case 2: An unused frame
    if (maxFrameIndex + 1 < NUM_FRAMES) {
        clearTable(maxFrameIndex + 1);
        return maxFrameIndex + 1;
    }

    //case 3: If all frames are already used
    PMevict(cyclicFrame, cyclicPage);
    clearTable(cyclicFrame);
    PMwrite(cyclicParent, 0);
    return cyclicFrame;
}
/**
* Helper method to initialize the variables for the traverse on the memory tree

*@param virtualAddress  the boundry of our search
*@param pageSwappedIn  the page we want to swap in
*/
uint64_t getNewFrameAddress(uint64_t virtualAddress, uint64_t pageSwappedIn) {
    uint64_t currParent = 0;
    uint64_t currLevel = 0;
    uint64_t currAddress = 0;
    uint64_t currFrame = 0;

    //empty case
    bool isEmpty = false;
    uint64_t emptyFrameAddress = 0;
    uint64_t emptyFrameParent = 0;

    //unused frame
    uint64_t maxFrameIndex = 0;

    //all frames are already used
    uint64_t maxCyclicDistance = 0;
    uint64_t cyclicParent = 0;
    uint64_t cyclicFrame = 0;
    uint64_t cyclicPage = 0;

    treeTraverse(virtualAddress, currAddress, currFrame, currParent,
                 currLevel, pageSwappedIn,
                 &isEmpty, &emptyFrameAddress, &emptyFrameParent,
                 &maxFrameIndex,
                 &maxCyclicDistance, &cyclicFrame,
                 &cyclicPage, &cyclicParent);

    return getFrameAddressByCases(isEmpty, emptyFrameAddress, emptyFrameParent, maxFrameIndex, cyclicParent,
                                  cyclicFrame, cyclicPage);
}
/**
*Function get a virtual address and find a corresponding frame to read from  or write into
*@param virtualAddress the address in our virtual memory.
*/
uint64_t getFrames(uint64_t virtualAddress) {
    word_t value = 0;
    uint64_t currAddress = 0;
    uint64_t page = ((1 << (VIRTUAL_ADDRESS_WIDTH - OFFSET_WIDTH)) - 1) & (virtualAddress >> OFFSET_WIDTH);
    // we want to calculate rootSize as it won't necessarily equal to OFFSET_WIDTH
    uint64_t rootSize = VIRTUAL_ADDRESS_WIDTH % OFFSET_WIDTH;
    if (rootSize == 0) {
        rootSize = OFFSET_WIDTH;
    }
    for (uint64_t i = 0; i < TABLES_DEPTH; i++) {
        uint64_t currLevel;
        if (i == 0) {
            currLevel = ((1 << rootSize) - 1) & (virtualAddress >> (TABLES_DEPTH * OFFSET_WIDTH));
        }
        else {
            currLevel = ((1 << OFFSET_WIDTH) - 1) & (virtualAddress >> ((TABLES_DEPTH - i) * OFFSET_WIDTH));
        }
        PMread(currAddress * PAGE_SIZE + currLevel, &value);
        if (value == 0) {
            //getting the address of the new available frame
            uint64_t newFrameAddress = getNewFrameAddress(currAddress, page);
            // in case we couldn't get an address for the new frame
            if (newFrameAddress == 0)
                return 0;
            PMwrite(currAddress * PAGE_SIZE + currLevel, (word_t) newFrameAddress);
            currAddress = newFrameAddress;
        }
        else {
            currAddress = value;
        }
    }
    PMrestore(currAddress, page);
    return currAddress;
}

uint64_t getOffset(uint64_t virtualAddress) {
    uint64_t mask = (1 << OFFSET_WIDTH) - 1;
    return virtualAddress & mask;
}

void VMinitialize() {
    for (uint64_t i = 0; i < PAGE_SIZE; i++) {
        PMwrite(i, 0);
    }
}

int VMread(uint64_t virtualAddress, word_t* value) {
    if (virtualAddress < 0 || virtualAddress >= VIRTUAL_MEMORY_SIZE) {
        return 0;
    }
    uint64_t offset = getOffset(virtualAddress);
    uint64_t frames = getFrames(virtualAddress);
    uint64_t physicalAddress = (frames << OFFSET_WIDTH) + offset;
    PMread(physicalAddress, value);
    return 1;
}

int VMwrite(uint64_t virtualAddress, word_t value) {
    if (virtualAddress < 0 || virtualAddress >= VIRTUAL_MEMORY_SIZE) {
        return 0;
    }
    uint64_t offset = getOffset(virtualAddress);
    uint64_t frames = getFrames(virtualAddress);
    uint64_t physicalAddress = (frames << OFFSET_WIDTH) + offset;
    PMwrite(physicalAddress, value);
    return 1;
}
