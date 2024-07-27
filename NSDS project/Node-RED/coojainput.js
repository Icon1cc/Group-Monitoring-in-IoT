function extractMembersSender() {
    let topicParts = msg.topic.split('/');
    let sender = topicParts[2];

    // Ensure 'members' property exists in msg.payload
    if (!msg.payload || !msg.payload.hasOwnProperty('members')) {
        msg.payload = {
            sender: sender,
            members: [sender]
        };
    } else {
        // If 'members' property exists, add sender to the array
        msg.payload.members.push(sender);
    }

    return msg.payload;
}

// Clean the input
msg.payload = extractMembersSender();

return msg;
