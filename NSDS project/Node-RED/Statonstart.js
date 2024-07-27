let groups = context.get('groups') || {};
let dismantle_group = context.get('dismantle_groups') || {};

// Define the number of groups.
const numMotes = 8
const numGroups = Math.floor(numMotes / 3);

// Create groups.
for (let k = 1; k <= numGroups; k++) {
    let groupName = "group " + k;

    groups["group" + k] = {
        name: groupName,
        members: [],
        cardinality: 0,
        maximum: 0,
        minimum: 0,
        average: 0,
        index: 0,
        timestamp: 0,
        lifetime: 0,
        dismantle_timer: 0
    };
}

context.set('groups', groups);
context.set('dismantle_group', dismantle_group);