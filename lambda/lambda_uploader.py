"""
Lambda Function: Spresense Image Uploader

Receives JPEG image data from Spresense via API Gateway (HTTPS POST)
and uploads it directly to an S3 bucket.

Triggered by: API Gateway POST /image-upload
Output: JPEG file saved to S3 under images/

Environment Variables:
  BUCKET_NAME - S3 bucket name to store uploaded images

IAM Permissions Required:
  - AmazonS3FullAccess (for s3:PutObject)

License: MIT
"""

import boto3
import json
import base64
import uuid
import os
from datetime import datetime

# Load bucket name from environment variable
BUCKET_NAME = os.environ.get('BUCKET_NAME', 'your-bucket-name')


def lambda_handler(event, context):
    """
    Receives image binary data from API Gateway and saves it to S3.

    API Gateway passes binary data as base64-encoded string with
    isBase64Encoded = True when Content-Type is image/jpeg.

    Args:
        event:   API Gateway event object
        context: Lambda context object

    Returns:
        dict: HTTP response with status code and JSON body
    """
    try:
        # Decode image data
        # API Gateway base64-encodes binary payloads automatically
        if event.get('isBase64Encoded', False):
            image_data = base64.b64decode(event['body'])
        else:
            # Fallback for direct invocation or testing
            body = event['body']
            image_data = body.encode() if isinstance(body, str) else body

        # Generate unique filename using UTC timestamp + UUID
        timestamp = datetime.utcnow().strftime('%Y-%m-%d_%H-%M-%S')
        unique_filename = f"{timestamp}_{uuid.uuid4()}.jpg"
        key = f"images/{unique_filename}"

        # Upload image directly to S3
        s3 = boto3.client('s3')
        s3.put_object(
            Bucket=BUCKET_NAME,
            Key=key,
            Body=image_data,
            ContentType='image/jpeg'
        )

        print(f"Upload successful: s3://{BUCKET_NAME}/{key}")

        # Build S3 URL for response
        s3_url = f"https://{BUCKET_NAME}.s3.ap-northeast-1.amazonaws.com/{key}"

        return {
            'statusCode': 200,
            'headers': {
                'Content-Type': 'application/json',
                'Access-Control-Allow-Origin': '*',
                'Access-Control-Allow-Methods': 'POST,OPTIONS',
                'Access-Control-Allow-Headers': 'Content-Type'
            },
            'body': json.dumps({
                'message': 'Upload successful',
                'key': key,
                's3_url': s3_url,
                'timestamp': timestamp
            })
        }

    except Exception as e:
        print(f"Error: {str(e)}")
        import traceback
        traceback.print_exc()

        return {
            'statusCode': 500,
            'headers': {
                'Content-Type': 'application/json',
                'Access-Control-Allow-Origin': '*'
            },
            'body': json.dumps({
                'error': 'Upload failed',
                'message': str(e)
            })
        }
