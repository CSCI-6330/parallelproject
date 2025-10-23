terraform {
  required_providers {
    aws = {
      source  = "hashicorp/aws"
      version = "~> 5.0"
    }
  }
}

provider "aws" {
  region = "us-east-1"
}

# -----------------------------------------------------------------------------
# 1. S3 bucket for EMR logs (you already had this)
# -----------------------------------------------------------------------------
resource "aws_s3_bucket" "demo" {
  bucket = "kafka-bucket-2025"
}

# -----------------------------------------------------------------------------
# 2. IAM roles required by EMR
# -----------------------------------------------------------------------------

# EMR service role (for EMR to manage itself)
resource "aws_iam_role" "emr_service_role" {
  name = "emr_service_role_tf"
  assume_role_policy = jsonencode({
    Version = "2012-10-17"
    Statement = [{
      Effect = "Allow"
      Principal = { Service = "elasticmapreduce.amazonaws.com" }
      Action = "sts:AssumeRole"
    }]
  })
}

resource "aws_iam_role_policy_attachment" "emr_service_role_policy" {
  role       = aws_iam_role.emr_service_role.name
  policy_arn = "arn:aws:iam::aws:policy/service-role/AmazonElasticMapReduceRole"
}

# EMR EC2 instance role (for cluster nodes)
resource "aws_iam_role" "emr_ec2_role" {
  name = "emr_ec2_role_tf"
  assume_role_policy = jsonencode({
    Version = "2012-10-17"
    Statement = [{
      Effect = "Allow"
      Principal = { Service = "ec2.amazonaws.com" }
      Action = "sts:AssumeRole"
    }]
  })
}

resource "aws_iam_role_policy_attachment" "emr_ec2_policy" {
  role       = aws_iam_role.emr_ec2_role.name
  policy_arn = "arn:aws:iam::aws:policy/service-role/AmazonElasticMapReduceforEC2Role"
}

# Instance profile so EC2 instances can use the above role
resource "aws_iam_instance_profile" "emr_profile" {
  name = "emr_instance_profile_tf"
  role = aws_iam_role.emr_ec2_role.name
}

# -----------------------------------------------------------------------------
# 3. Use default VPC and subnet for simplicity
# -----------------------------------------------------------------------------
data "aws_vpc" "default" {
  default = true
}

data "aws_subnets" "default" {
  filter {
    name   = "vpc-id"
    values = [data.aws_vpc.default.id]
  }
}

locals {
  subnet_id = data.aws_subnets.default.ids[0]
}

# -----------------------------------------------------------------------------
# 4. Create the EMR cluster
# -----------------------------------------------------------------------------
resource "aws_emr_cluster" "emr_cluster" {
  name          = "Kafka-EMR-Cluster"
  release_label = "emr-6.15.0"    # EMR version
  applications  = ["Hadoop", "Spark"]

  service_role = aws_iam_role.emr_service_role.name

  ec2_attributes {
  subnet_id        = local.subnet_id
  instance_profile = aws_iam_instance_profile.emr_profile.arn
  key_name         = null   # ← remove or set to null
  }

  master_instance_group {
    instance_type = "m5.xlarge"
    ebs_config {
      size                 = 64
      type                 = "gp3"
      volumes_per_instance = 1
    }
  }

  core_instance_group {
    instance_type  = "m5.xlarge"
    instance_count = 2
    ebs_config {
      size                 = 128
      type                 = "gp3"
      volumes_per_instance = 1
    }
  }

  log_uri = "s3://${aws_s3_bucket.demo.bucket}/emr-logs/"

  keep_job_flow_alive_when_no_steps = true
  termination_protection            = false

  auto_termination_policy {
    idle_timeout = 3600  # 1 hour
  }

  tags = {
    Name    = "Kafka-EMR-Cluster"
    Project = "KafkaDataPipeline"
  }
}

# -----------------------------------------------------------------------------
# 5. Output key info
# -----------------------------------------------------------------------------
output "emr_cluster_id" {
  value = aws_emr_cluster.emr_cluster.id
}

output "emr_master_public_dns" {
  value = aws_emr_cluster.emr_cluster.master_public_dns
}
