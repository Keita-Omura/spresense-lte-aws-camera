"""
Lambda Function: Spresense Image Email Sender

Triggered by S3 ObjectCreated events when a JPEG image is uploaded.
Downloads the image from S3 and sends it as an email attachment via SES.

Triggered by: S3 ObjectCreated event (suffix: .jpg)
Input: S3 event containing bucket name and object key

Environment Variables (optional - currently hardcoded for simplicity):
  SENDER_EMAIL    - Verified SES sender address
  RECIPIENT_EMAIL - Email address to receive notifications

IAM Permissions Required:
  - AmazonS3ReadOnlyAccess (for s3:GetObject)
  - AmazonSESFullAccess    (for ses:SendRawEmail)

Note: Both sender and recipient must be verified in SES
      when operating in SES Sandbox mode.

License: MIT
Author: Keita Omura
Repository: https://github.com/Keita-Omura/spresense-lte-aws-camera
"""

import json
import boto3
import os
from email.mime.multipart import MIMEMultipart
from email.mime.text import MIMEText
from email.mime.application import MIMEApplication
from datetime import datetime

# Email configuration
# Replace with your own verified SES email addresses
SENDER_EMAIL    = os.environ.get('SENDER_EMAIL',    'sender@example.com')
RECIPIENT_EMAIL = os.environ.get('RECIPIENT_EMAIL', 'recipient@example.com')
REGION          = 'ap-northeast-1'

# AWS clients
s3_client  = boto3.client('s3')
ses_client = boto3.client('ses', region_name=REGION)


def lambda_handler(event, context):
    """
    Downloads image from S3 and sends it as an email attachment via SES.

    Args:
        event:   S3 ObjectCreated event notification
        context: Lambda context object

    Returns:
        dict: Response with status code and message
    """
    try:
        print("Event received:", json.dumps(event))

        # Extract bucket and object key from S3 event
        bucket   = event['Records'][0]['s3']['bucket']['name']
        key      = event['Records'][0]['s3']['object']['key']
        filesize = event['Records'][0]['s3']['object']['size']

        print(f"Bucket: {bucket}, Key: {key}, Size: {filesize} bytes")

        # Extract filename from key (e.g. "images/2026-04-14_22-45-30_uuid.jpg")
        filename = key.split('/')[-1]

        # Download image from S3
        print(f"Downloading image from S3: {bucket}/{key}")
        s3_response = s3_client.get_object(Bucket=bucket, Key=key)
        image_data  = s3_response['Body'].read()
        print(f"Downloaded: {len(image_data)} bytes")

        # Build email with image attachment
        msg = MIMEMultipart('mixed')
        msg['Subject'] = f"📸 Spresense Camera Upload - {filename}"
        msg['From']    = SENDER_EMAIL
        msg['To']      = RECIPIENT_EMAIL

        # Plain text body
        text_part = MIMEText(
            create_email_body(filename, filesize, key),
            'plain',
            'utf-8'
        )
        msg.attach(text_part)

        # Attach image file
        attachment = MIMEApplication(image_data)
        attachment.add_header(
            'Content-Disposition', 'attachment', filename=filename
        )
        msg.attach(attachment)

        # Send via SES
        print(f"Sending email to {RECIPIENT_EMAIL}")
        response = ses_client.send_raw_email(
            Source=SENDER_EMAIL,
            Destinations=[RECIPIENT_EMAIL],
            RawMessage={'Data': msg.as_string()}
        )

        print(f"Email sent! Message ID: {response['MessageId']}")

        return {
            'statusCode': 200,
            'body': json.dumps({
                'message': 'Email sent successfully',
                'messageId': response['MessageId'],
                'filename': filename,
                'size': filesize
            })
        }

    except Exception as e:
        print(f"Error: {str(e)}")
        import traceback
        traceback.print_exc()

        return {
            'statusCode': 500,
            'body': json.dumps({
                'error': 'Failed to send email',
                'message': str(e)
            })
        }


def create_email_body(filename: str, file_size: int, s3_key: str) -> str:
    """
    Generate plain text email body.

    Args:
        filename:  Name of the uploaded image file
        file_size: File size in bytes
        s3_key:    S3 object key

    Returns:
        Formatted email body string
    """
    now           = datetime.now().strftime('%Y-%m-%d %H:%M:%S UTC')
    file_size_kb  = file_size / 1024

    return f"""
Spresense LTE Camera System
━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

A new image has been uploaded from your Spresense camera.

📸 File Information
  Filename  : {filename}
  File Size : {file_size_kb:.1f} KB
  S3 Path   : {s3_key}
  Timestamp : {now}

📎 Attachment
  The image file is attached to this email.

━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
Automatically sent by AWS Lambda.
Spresense LTE Camera to AWS Pipeline
"""
