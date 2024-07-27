// Helper function to update group statistics
function updateGroupStatistics(group, newCardinality) {
    group.cardinality = newCardinality;
    group.lifetime = (Date.now() - group.timestamp) / 1000;

    // Update max, min, and average cardinalities
    group.maximum = Math.max(group.maximum, newCardinality);
    group.minimum = Math.min(group.minimum || Infinity, newCardinality);
    group.average = ((group.average * group.index) + newCardinality) / (group.index + 1);
    group.index += 1;
}


// Function to handle the periodic monitoring of groups and timeouts
function periodicGroupMonitoring() {
    const groups = context.get('groups') || {};
    const now = Date.now();
    const timeoutThreshold = 60000; // Timeout threshold in milliseconds (e.g., 1 minute)

    Object.keys(groups).forEach(groupKey => {
        const group = groups[groupKey];
        
        // Filter out members who have timed out
        group.members = group.members.filter(member => 
            now - (member.lastActive || group.timestamp) < timeoutThreshold
        );

        // Update statistics if group cardinality has changed
        const newCardinality = group.members.length;
        if (newCardinality !== group.cardinality) {
            updateGroupStatistics(group, newCardinality);
        }
        
        // Check if group should be dismantled due to low cardinality
        if (group.cardinality < 3) {
            dismantleGroup(groupKey, groups);
        } else {
            // Update group's lifetime
            group.lifetime = (now - group.timestamp) / 1000;
        }
    });

    context.set('groups', groups);
}

// Function to dismantle a group
function dismantleGroup(groupKey, groups) {
    // Log a warning or perform other needed dismantling logic
    node.warn(`Group ${groupKey} has been dismantled due to insufficient members.`);
    groups[groupKey] = {
        members: [],
        cardinality: 0,
        maximum: 0,
        minimum: 0,
        average: 0,
        index: 0,
        timestamp: Date.now(),
        lifetime: 0,
        dismantle_timer: Date.now()
    };
}

// Function to update a group when a message is received
function processGroupMemberships(cooja_result) {
    const groups = context.get('groups') || {};
    const now = Date.now();
    
    // Find or create groups for the sender
    let existingGroups = checkSenderInGroups(cooja_result);
    
    existingGroups.forEach(group => {
        // Update last active time for each member
        group.members.forEach(member => {
            if (member.id === cooja_result.sender) {
                member.lastActive = now;
            }
        });

        // Check if current members match the new member list
        if (!arraysEqual(group.members.map(m => m.id).sort(), cooja_result.members.sort())) {
            // Members have changed, update the group
            group.members = cooja_result.members.map(id => ({ id, lastActive: now }));
            updateGroupStatistics(group, group.members.length);
        }
        // Otherwise, the group remains unchanged so no need to update statistics
    });
    
    context.set('groups', groups);
}

// Function to check if the sender is in any group and return those groups
function checkSenderInGroups(cooja_result) {
    const groups = context.get('groups');
    let memberGroups = [];

    // Check all groups to see if sender is a member
    Object.keys(groups).forEach(groupKey => {
        if (groups[groupKey].members.includes(cooja_result.sender)) {
            memberGroups.push(groups[groupKey]);
        }
    });

    if (memberGroups.length === 0) {
        addNewGroup(cooja_result, groups);
    }

    return memberGroups;
}

// Function to add a new group for the sender
function addNewGroup(cooja_result, groups) {
    for (let k = 1; k <= Object.keys(groups).length; k++) {
        let groupKey = "group" + k;
        if (groups[groupKey].members.length === 0) {
            groups[groupKey].members = [...cooja_result.members, cooja_result.sender];
            const newCardinality = groups[groupKey].members.length;

            // Initialize group statistics
            updateGroupStatistics(groups[groupKey], newCardinality);
            groups[groupKey].timestamp = Date.now(); // Set the creation time of the group

            context.set("groups", groups);
            return groups[groupKey];
        }
    }
    return null; // In case all groups are full
}

// Function to process each member's group status
function processGroupMemberships(cooja_result) {
    const groups = context.get("groups");
    let existingGroups = checkSenderInGroups(cooja_result);

    existingGroups.forEach(group => {
        // Check if current members match the new member list
        if (arraysEqual(group.members.sort(), cooja_result.members.sort())) {
            // Members have not changed, update group statistics
            updateGroupStatistics(group, group.members.length);
        } else {
            // Members have changed, process the update
            group.members = cooja_result.members;
            const newCardinality = group.members.length;

            // Update statistics with new cardinality
            updateGroupStatistics(group, newCardinality);
        }
    });

    context.set("groups", groups);
}

// Function to compare two arrays for equality
function arraysEqual(arr1, arr2) {
    if (arr1.length !== arr2.length) return false;
    for (let i = 0; i < arr1.length; i++) {
        if (arr1[i] !== arr2[i]) return false;
    }
    return true;
}


// Function to handle group survivability
function survivability() {
    const groups = context.get("groups");
    let dismantledGroups = [];

    Object.keys(groups).forEach(groupKey => {
        if (groups[groupKey].cardinality < 3) {
            // Group is too small, dismantle it
            dismantledGroups.push(groupKey);
            groups[groupKey] = {
                members: [],
                cardinality: 0,
                maximum: 0,
                minimum: 0,
                average: 0,
                index: 0,
                timestamp: Date.now(),
                lifetime: 0,
                dismantle_timer: Date.now()
            };
        }
    });

    context.set("groups", groups);
    return dismantledGroups;
}

// Main execution flow
const cooja_result = msg.payload;
processGroupMemberships(cooja_result);
const dismantledGroups = survivability();

// Display messages for dismantled groups
if(dismantledGroups.length > 0) {
    dismantledGroups.forEach(groupKey => {
        node.warn(`Group ${groupKey} has been dismantled due to insufficient members.`);
    });
}

// Return the updated group information
msg.payload = context.get("groups");
return msg;

